// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusDeleteAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "ObjectTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utils/NexusPropertyUtils.h"
#include "NexusMcpTool.h"

void FDeleteAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("delete_asset");
	Out.Description = TEXT("Delete single asset package. Best-effort redirector cleanup; irreversible.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset path, e.g. '/Game/BP/BP_MyActor'")))
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
		const FNexusArgs A(Arguments);


		const FString DeletePath = A.Str(TEXT("assetPath"));

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
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *PackagePath));
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
				FString::Printf(TEXT("%s (delete failed, may be referenced)"), *PackagePath));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FDeleteAssetCapability)

#endif // WITH_EDITOR
