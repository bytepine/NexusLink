// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusDeleteAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "ObjectTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utils/NexusPropertyUtils.h"
#include "NexusMcpTool.h"

void FDeleteAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("delete_asset");
	Out.Description = TEXT("永久删除单个资产包。尽力清理重定向器；不可逆。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产路径，如 '/Game/BP/BP_MyActor'")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("trash"), TEXT("remove"), TEXT("package"), TEXT("uasset"), TEXT("cleanup") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("rename_asset") };
}

FCapabilityResult FDeleteAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("缺少参数");
			return;
		}

		FString DeletePath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), DeletePath) || DeletePath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FString PackagePath = DeletePath;
		int32 DotIdx;
		if (PackagePath.FindLastChar(TEXT('.'), DotIdx))
		{
			PackagePath = PackagePath.Left(DotIdx);
		}

		TArray<FAssetData> Assets;
		Registry.GetAssetsByPackageName(FName(*PackagePath), Assets);
		if (Assets.Num() == 0)
		{
			UObject* Obj = LoadObject<UObject>(nullptr, *DeletePath);
			if (!Obj)
			{
				Obj = LoadObject<UObject>(nullptr, *(PackagePath + TEXT(".") + FPaths::GetBaseFilename(PackagePath)));
			}
			if (Obj)
			{
				Assets.Add(FAssetData(Obj));
			}
		}

		if (Assets.Num() == 0)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("%s（资产未找到）"), *PackagePath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		TArray<UObject*> ObjectsToDelete;
		for (const FAssetData& AD : Assets)
		{
			UObject* Obj = AD.GetAsset();
			if (Obj)
			{
				ObjectsToDelete.Add(Obj);
			}
		}

		const int32 Count = ObjectsToDelete.Num() > 0 ? ObjectTools::DeleteObjects(ObjectsToDelete, false) : 0;
		const bool bDeleted = Count > 0;

		if (bDeleted)
		{
			TArray<FAssetData> Redirectors;
			TArray<FAssetData> RemainAssets;
			Registry.GetAssetsByPackageName(FName(*PackagePath), RemainAssets);
			for (const FAssetData& AD : RemainAssets)
			{
				if (NEXUS_ASSET_CLASS_NAME(AD).ToString().Contains(TEXT("ObjectRedirector")))
				{
					Redirectors.Add(AD);
				}
			}
			if (Redirectors.Num() > 0)
			{
				TArray<UObject*> RedirObjs;
				for (const FAssetData& AD : Redirectors)
				{
					UObject* Obj = AD.GetAsset();
					if (Obj)
					{
						RedirObjs.Add(Obj);
					}
				}
				if (RedirObjs.Num() > 0)
				{
					ObjectTools::DeleteObjects(RedirObjs, false);
				}
			}

			FString FilePath;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FilePath, FPackageName::GetAssetPackageExtension()) &&
				!FPaths::FileExists(FilePath))
			{
				TArray<FString> DeletedFilePaths = { FilePath };
				Registry.ScanModifiedAssetFiles(DeletedFilePaths);
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (bDeleted)
		{
		}
		else
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"),
				FString::Printf(TEXT("%s（删除失败，可能被引用）"), *PackagePath));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FDeleteAssetCapability)

#endif // WITH_EDITOR
