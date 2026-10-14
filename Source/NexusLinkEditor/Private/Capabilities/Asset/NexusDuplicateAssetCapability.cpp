// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusDuplicateAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NexusMcpTool.h"

void FNexusDuplicateAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("DuplicateAsset");
	Out.Description = TEXT("Copy editor asset to new path. Source unchanged.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),     FNexusSchema::Str(TEXT("Source asset path")))
		.Prop(TEXT("DestAssetPath"), FNexusSchema::Str(TEXT("Target full asset path (package + name)")))
		.Required({ TEXT("AssetPath"), TEXT("DestAssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("copy"), TEXT("clone"), TEXT("duplicate"), TEXT("blueprint"), TEXT("bp") };
	Out.RelatedCapabilities = { TEXT("RenameAsset"), TEXT("DeleteAsset") };
}

FCapabilityResult FNexusDuplicateAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString SrcPath = A.Str(TEXT("AssetPath"));
		const FString DestAssetPath = A.Str(TEXT("DestAssetPath"));

		FText PathErrText;
		if (!FPackageName::IsValidLongPackageName(DestAssetPath, false, &PathErrText))
		{
			OutError = FString::Printf(TEXT("Invalid DestAssetPath: %s"), *PathErrText.ToString());
			return;
		}

		if (FPackageName::DoesPackageExist(DestAssetPath))
		{
			OutError = FString::Printf(TEXT("Target already exists: %s"), *DestAssetPath);
			return;
		}

		// 源资产容错加载：先按纯包路径，失败再补 .对象名
		UObject* SrcAsset = LoadObject<UObject>(nullptr, *SrcPath);
		if (!SrcAsset)
		{
			SrcAsset = LoadObject<UObject>(nullptr, *(SrcPath + TEXT(".") + FPaths::GetBaseFilename(SrcPath)));
		}
		if (!SrcAsset)
		{
			OutError = FString::Printf(TEXT("Asset not found: %s"), *SrcPath);
			return;
		}

		const FString NewName    = FPaths::GetBaseFilename(DestAssetPath);
		const FString NewPkgPath = FPaths::GetPath(DestAssetPath);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* NewAsset = AssetTools.DuplicateAsset(NewName, NewPkgPath, SrcAsset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!NewAsset)
		{
			Entry->SetBoolField(TEXT("bSuccess"), false);
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Copyfailed: %s -> %s"), *SrcPath, *DestAssetPath));
		}
		else
		{
			Entry->SetStringField(TEXT("SourcePath"), SrcPath);
			Entry->SetStringField(TEXT("DestAssetPath"), DestAssetPath);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusDuplicateAssetCapability)

#endif // WITH_EDITOR
