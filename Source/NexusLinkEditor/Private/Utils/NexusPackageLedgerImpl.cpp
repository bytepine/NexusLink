// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPackageLedgerImpl.h"
#include "NexusEditorServices.h"
#include "Utils/NexusVersionCompat.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#if WITH_EDITOR
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

namespace
{
	/** 对候选包做统一安全过滤（dirty / 引擎内建 / 编辑器已打开）后整批卸载；返回实际卸载数量。 */
	int32 ImplFlushPackages(const TArray<UPackage*>& Packages, bool bSkipDirty, bool bGC, TSet<UPackage*>* OutSkipped)
	{
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
				if (OutSkipped) OutSkipped->Add(Pkg);
				continue;
			}

			if (Pkg->HasAnyPackageFlags(PKG_CompiledIn) || Pkg == GetTransientPackage())
			{
				// 引擎内建 / transient 包，不应由本机制处理
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
				if (OutSkipped) OutSkipped->Add(Pkg);
				continue;
			}

			ToUnload.Add(Pkg);
		}

		int32 UnloadedCount = 0;
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
			UnloadedCount = ToUnload.Num();
		}

		if (bGC && UnloadedCount > 0)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		return UnloadedCount;
	}
}

void NexusInstallPackageLedgerHooks()
{
	FNexusEditorServices::FlushPackages = &ImplFlushPackages;
}

#else // !WITH_EDITOR

void NexusInstallPackageLedgerHooks() {}

#endif // WITH_EDITOR
