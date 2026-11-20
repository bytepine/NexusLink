// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorTransactionImpl.h"
#include "NexusEditorServices.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "Misc/ITransaction.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetDebugUtilities.h"
#if NX_UE_HAS_BLUEPRINT_PIN_WATCH_SETTINGS
#include "BlueprintEditorSettings.h"
#else
#include "Engine/Breakpoint.h"
#endif

namespace
{
	class FNexusScopedTransactionHandle;
	FNexusScopedTransactionHandle* ActiveHandle = nullptr;

	class FNexusScopedTransactionHandle final : public INexusTransactionHandle
	{
	public:
		FNexusScopedTransactionHandle(UTransBuffer* InBuffer, int32 InIndex)
			: Buffer(InBuffer), Record(InBuffer->UndoBuffer.Last()), Index(InIndex)
		{
			ActiveHandle = this;
		}

		virtual ~FNexusScopedTransactionHandle() override
		{
			if (ActiveHandle == this) ActiveHandle = nullptr;
			if (IsOutstanding())
			{
				GEditor->EndTransaction();
			}
		}

		virtual bool CancelAndRevert() override
		{
			if (!IsOutstanding()) return false;
			UTransBuffer* Trans = Buffer.Get();
			GEditor->CancelTransaction(Index);
			if (Record->GetRecordCount() == 0) return true;

			// Cancel restores any redo entries UE set aside at Begin. Undo only our retained
			// record so pin deserialization sees the engine's active undo context.
			const int32 PreviousRedoCount = Trans->UndoCount;
			Trans->UndoCount = 0;
			Trans->SetUndoBarrier();
			Trans->UndoBuffer.Add(Record);
			const bool bReverted = Trans->Undo(false);
			Trans->RemoveUndoBarrier();
			Trans->UndoCount = PreviousRedoCount;
			if (PreviousRedoCount > 0) Trans->OnUndoBufferChanged().Broadcast();
#if NX_UE_HAS_BLUEPRINT_PIN_WATCH_SETTINGS
			if (bReverted)
			{
				UBlueprintEditorSettings* Settings = GetMutableDefault<UBlueprintEditorSettings>();
				bool bChanged = false;
				for (const auto& Entry : DebugSettings)
				{
					UBlueprint* Blueprint = Entry.Key.Get();
					if (!Blueprint) continue;
					auto& Current = Settings->PerBlueprintSettings.FindOrAdd(Blueprint->GetPathName());
					if (Current == Entry.Value) continue;
					Current = Entry.Value;
					for (FBlueprintBreakpoint& Breakpoint : Current.Breakpoints)
						FKismetDebugUtilities::ReapplyBreakpoint(Breakpoint);
					FKismetDebugUtilities::WatchedPinsListChangedEvent.Broadcast(Blueprint);
					bChanged = true;
				}
				if (bChanged) Settings->SaveConfig();
			}
#else
			if (bReverted)
				for (const auto& WeakBlueprint : DebugBlueprints)
					if (UBlueprint* Blueprint = WeakBlueprint.Get())
					{
						for (UBreakpoint* Breakpoint : Blueprint->Breakpoints)
							if (Breakpoint) FKismetDebugUtilities::ReapplyBreakpoint(Breakpoint);
						FKismetDebugUtilities::WatchedPinsListChangedEvent.Broadcast(Blueprint);
					}
#endif
			return bReverted;
		}

		virtual bool IsOutstanding() const override
		{
			return GEditor && Buffer.IsValid() && GEditor->Trans == Buffer.Get() && GUndo == &Record.Get();
		}

		void RecordDebugSettings(UBlueprint* Blueprint)
		{
			if (!IsOutstanding()) return;
#if NX_UE_HAS_BLUEPRINT_PIN_WATCH_SETTINGS
			if (DebugSettings.Contains(Blueprint)) return;
			const auto& Settings = GetDefault<UBlueprintEditorSettings>()->PerBlueprintSettings;
			if (const FPerBlueprintSettings* Existing = Settings.Find(Blueprint->GetPathName()))
				DebugSettings.Add(Blueprint, *Existing);
#else
			if (DebugBlueprints.Contains(Blueprint) || (!Blueprint->Breakpoints.Num() && !Blueprint->WatchedPins.Num())) return;
			DebugBlueprints.Add(Blueprint);
			// UE4 clears the location inside each breakpoint UObject as well as the Blueprint array.
			for (UBreakpoint* Breakpoint : Blueprint->Breakpoints)
				if (Breakpoint)
				{
					Breakpoint->SetFlags(RF_Transactional);
					Breakpoint->Modify();
				}
#endif
		}

	private:
#if NX_UE_HAS_BLUEPRINT_PIN_WATCH_SETTINGS
		TMap<TWeakObjectPtr<UBlueprint>, FPerBlueprintSettings> DebugSettings;
#else
		TSet<TWeakObjectPtr<UBlueprint>> DebugBlueprints;
#endif
		TWeakObjectPtr<UTransBuffer> Buffer;
		TSharedRef<FTransaction> Record;
		int32 Index;
	};

	bool ImplIsTransactionActive()
	{
		return GEditor && GEditor->IsTransactionActive();
	}

	int32 ImplGetActiveRecordCount()
	{
		// 编辑器里 GUndo 恒为 UTransBuffer 用 FTransaction 建的，引擎自身（UTransBuffer::End）
		// 也做同样的向下转型；对象在 Modify() 时即入 Records，无需等 Finalize
		if (const FTransaction* Tx = static_cast<const FTransaction*>(GUndo))
		{
			return Tx->GetRecordCount();
		}
		return 0;
	}

	TUniquePtr<INexusTransactionHandle> ImplBeginTransaction(const FString& Title)
	{
		UTransBuffer* Buffer = GEditor ? Cast<UTransBuffer>(GEditor->Trans) : nullptr;
		if (!Buffer || GIsTransacting || ImplIsTransactionActive())
		{
			return nullptr;
		}
		const FString Session = FString::Printf(TEXT("MCP: %s"), Title.IsEmpty() ? TEXT("Capability") : *Title);
		const int32 Index = GEditor->BeginTransaction(FText::FromString(Session));
		if (Index == INDEX_NONE || !GUndo || Buffer->UndoBuffer.Num() == 0 || GUndo != &Buffer->UndoBuffer.Last().Get())
		{
			return nullptr;
		}
		return MakeUnique<FNexusScopedTransactionHandle>(Buffer, Index);
	}

	FDelegateHandle GNexusTransientWorldUndoHandle;

	/** 临时预览世界里的控件实例会被撤销记录抓住，紧接着的强制 GC 会变成致命世界泄漏。 */
	void DropUndoHoldingTransientWorld(UWorld* World, bool, bool)
	{
		if (!GEditor || !World || World->GetOutermost() != GetTransientPackage()) return;
		GEditor->ResetTransaction(FText::FromString(TEXT("NexusLink released undo records in a transient preview world")));
	}
}

void NexusRecordBlueprintDebugSettings(UBlueprint* Blueprint)
{
	if (ActiveHandle && Blueprint) ActiveHandle->RecordDebugSettings(Blueprint);
}

void NexusInstallEditorTransactionHooks()
{
	FNexusEditorServices::IsTransactionActive = &ImplIsTransactionActive;
	FNexusEditorServices::GetActiveTransactionRecordCount = &ImplGetActiveRecordCount;
	FNexusEditorServices::BeginTransaction = &ImplBeginTransaction;
	if (!GNexusTransientWorldUndoHandle.IsValid())
	{
		GNexusTransientWorldUndoHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&DropUndoHoldingTransientWorld);
	}
}

void NexusShutdownEditorTransactionHooks()
{
	if (GNexusTransientWorldUndoHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(GNexusTransientWorldUndoHandle);
		GNexusTransientWorldUndoHandle.Reset();
	}
}

#else // !WITH_EDITOR

void NexusInstallEditorTransactionHooks() {}

#endif // WITH_EDITOR
