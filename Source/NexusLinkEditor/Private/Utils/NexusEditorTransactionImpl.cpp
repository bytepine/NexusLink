// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorTransactionImpl.h"
#include "NexusEditorServices.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "Misc/ITransaction.h"

namespace
{
	class FNexusScopedTransactionHandle final : public INexusTransactionHandle
	{
	public:
		FNexusScopedTransactionHandle(UTransBuffer* InBuffer, int32 InIndex)
			: Buffer(InBuffer), Record(InBuffer->UndoBuffer.Last()), Index(InIndex)
		{}

		virtual ~FNexusScopedTransactionHandle() override
		{
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
			return bReverted;
		}

		virtual bool IsOutstanding() const override
		{
			return GEditor && Buffer.IsValid() && GEditor->Trans == Buffer.Get() && GUndo == &Record.Get();
		}

	private:
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
		if (!Buffer || GIsTransacting || !GEditor->CanTransact() || ImplIsTransactionActive())
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
