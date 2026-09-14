// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPackageLedger.h"
#include "NexusLinkSettings.h"
#include "NexusEditorServices.h"
#include "UObject/Package.h"
#include "HAL/PlatformMemory.h"

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
	const TArray<UPackage*>& Packages, bool bSkipDirty, bool bGC, TSet<UPackage*>* OutSkipped)
{
	FFlushStats Stats;

	if (!IsInGameThread())
	{
		return Stats;
	}

	// 实际卸载逻辑（UPackageTools / AssetEditorSubsystem / GEditor）经钩子转发到 NexusLinkEditor；
	// 未安装钩子（独立 Game/DS 包）时钩子默认实现直接返回 0，等价于「无编辑器可卸载」。
	Stats.Unloaded = FNexusEditorServices::FlushPackages(Packages, bSkipDirty, bGC, OutSkipped);
	Stats.Skipped = OutSkipped ? OutSkipped->Num() : FMath::Max(0, Packages.Num() - Stats.Unloaded);
	return Stats;
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

	TSet<UPackage*> Skipped;
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
	if (!Ledger.IsSuppressedForThisCall() && Ledger.LiveCount() > 0)
	{
		Ledger.Flush(true);
	}
}
