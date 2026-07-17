// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPackageLedger.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusLinkSettings.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "HAL/PlatformMemory.h"

#if WITH_EDITOR
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#endif

FNexusPackageLedger& FNexusPackageLedger::Get()
{
	static FNexusPackageLedger Instance;
	return Instance;
}

void FNexusPackageLedger::NoteIntroduced(UPackage* Package)
{
	if (!Package)
	{
		return;
	}
	const FName PackageName = Package->GetFName();
	for (const FEntry& Existing : Entries)
	{
		if (Existing.PackageName == PackageName)
		{
			return; // 已登记，避免重复
		}
	}
	FEntry NewEntry;
	NewEntry.PackageName = PackageName;
	NewEntry.WeakPkg = Package;
	Entries.Add(NewEntry);
}

void FNexusPackageLedger::PruneDead()
{
	// WeakPkg 失效 = 引擎已自行回收该包，静默剔除（不计入错误，不重复处理）
	Entries.RemoveAll([](const FEntry& E) { return !E.WeakPkg.IsValid(); });
}

int32 FNexusPackageLedger::LiveCount()
{
	PruneDead();
	return Entries.Num();
}

void FNexusPackageLedger::ResetBaseline()
{
	BaselineUsedPhysical = FPlatformMemory::GetStats().UsedPhysical;
	bSuppressedForThisCall = false;
}

bool FNexusPackageLedger::ShouldFlush(int32 FlushThresholdCount, int32 MemoryHighWaterMB)
{
	if (bSuppressedForThisCall)
	{
		return false;
	}
	if (FlushThresholdCount > 0 && LiveCount() >= FlushThresholdCount)
	{
		return true;
	}
	if (MemoryHighWaterMB > 0)
	{
		const uint64 UsedNow = FPlatformMemory::GetStats().UsedPhysical;
		const uint64 GrowthBytes = (UsedNow > BaselineUsedPhysical) ? (UsedNow - BaselineUsedPhysical) : 0;
		const uint64 ThresholdBytes = static_cast<uint64>(MemoryHighWaterMB) * 1024ull * 1024ull;
		if (GrowthBytes >= ThresholdBytes)
		{
			return true;
		}
	}
	return false;
}

FNexusPackageLedger::FFlushStats FNexusPackageLedger::UnloadPackagesSafely(
	const TArray<UPackage*>& Packages, bool bSkipDirty, bool bGC, TArray<UPackage*>* OutSkipped)
{
	FFlushStats Stats;

#if !WITH_EDITOR
	return Stats;
#else
	if (!IsInGameThread())
	{
		return Stats;
	}

	TArray<UPackage*> ToUnload;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;

	for (UPackage* Pkg : Packages)
	{
		if (!Pkg)
		{
			continue;
		}

		if (bSkipDirty && Pkg->IsDirty())
		{
			// 保护未保存修改
			Stats.Skipped++;
			if (OutSkipped) OutSkipped->Add(Pkg);
			continue;
		}

		if (Pkg->HasAnyPackageFlags(PKG_CompiledIn) || Pkg == GetTransientPackage())
		{
			// 引擎内建 / transient 包，不应由本机制处理
			Stats.Skipped++;
			if (OutSkipped) OutSkipped->Add(Pkg);
			continue;
		}

		bool bHasOpenEditor = false;
		if (AssetEditorSubsystem)
		{
			UObject* PrimaryAsset = nullptr;
			ForEachObjectWithPackage(Pkg, [&PrimaryAsset](UObject* Obj)
			{
				if (Obj && !Obj->IsA(UPackage::StaticClass()) && Obj->HasAllFlags(RF_Public | RF_Standalone))
				{
					PrimaryAsset = Obj;
					return false;
				}
				return true;
			});
			if (PrimaryAsset && AssetEditorSubsystem->FindEditorsForAsset(PrimaryAsset).Num() > 0)
			{
				bHasOpenEditor = true;
			}
		}
		if (bHasOpenEditor)
		{
			// 用户正在编辑该资产
			Stats.Skipped++;
			if (OutSkipped) OutSkipped->Add(Pkg);
			continue;
		}

		ToUnload.Add(Pkg);
	}

	if (ToUnload.Num() > 0)
	{
		FText ErrorMsg;
#if NX_UE_HAS_UNLOAD_PACKAGES_DIRTY_FLAG
		UPackageTools::UnloadPackages(ToUnload, ErrorMsg, /*bUnloadDirtyPackages=*/!bSkipDirty);
#else
		// UE 4.26/4.27：UnloadPackages 无 bUnloadDirtyPackages 参数；本函数已在上面按 bSkipDirty
		// 过滤掉 dirty 包，ToUnload 中若仍含 dirty 包（bSkipDirty=false 强制卸载）则交由引擎默认行为处理。
		UPackageTools::UnloadPackages(ToUnload, ErrorMsg);
#endif
		Stats.Unloaded = ToUnload.Num();
	}

	if (bGC)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	return Stats;
#endif
}

FNexusPackageLedger::FFlushStats FNexusPackageLedger::Flush(bool bGC)
{
	FFlushStats Stats;

	TArray<UPackage*> Candidates;
	for (FEntry& E : Entries)
	{
		if (UPackage* Pkg = E.WeakPkg.Get())
		{
			Candidates.Add(Pkg);
		}
		else
		{
			// 引擎已自行回收：本条视为已释放，静默剔除，不当错误
			Stats.AlreadyCollected++;
		}
	}

	TArray<UPackage*> Skipped;
	const FFlushStats SubStats = UnloadPackagesSafely(Candidates, /*bSkipDirty=*/true, bGC, &Skipped);
	Stats.Unloaded = SubStats.Unloaded;
	Stats.Skipped = SubStats.Skipped;

	// 仅保留被跳过（dirty / 编辑器打开中）的条目，留待下轮 Flush 再看
	Entries.RemoveAll([&Skipped](const FEntry& E)
	{
		UPackage* Pkg = E.WeakPkg.Get();
		return !Pkg || !Skipped.Contains(Pkg);
	});

	BaselineUsedPhysical = FPlatformMemory::GetStats().UsedPhysical;
	return Stats;
}

void FNexusPackageLedger::MaybeFlush()
{
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!Settings || !Settings->bAutoUnloadIntrospectedPackages)
	{
		return;
	}
	FNexusPackageLedger& Ledger = Get();
	if (Ledger.ShouldFlush(Settings->FlushThresholdCount, Settings->MemoryHighWaterMB))
	{
		Ledger.Flush(true);
	}
}

void FNexusPackageLedger::FlushRemainingUnlessSuppressed()
{
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!Settings || !Settings->bAutoUnloadIntrospectedPackages)
	{
		return;
	}
	FNexusPackageLedger& Ledger = Get();
	if (!Ledger.IsSuppressedForThisCall())
	{
		Ledger.Flush(true);
	}
}
