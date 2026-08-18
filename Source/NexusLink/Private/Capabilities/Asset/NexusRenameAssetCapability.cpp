// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusRenameAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NexusMcpTool.h"

void FRenameAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("rename_asset");
	Out.Description = TEXT("Move or rename asset. Auto-generates redirectors.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),     FNexusSchema::Str(TEXT("Current asset path")))
		.Prop(TEXT("destAssetPath"), FNexusSchema::Str(TEXT("Target full asset path")))
		.Required({ TEXT("assetPath"), TEXT("destAssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("move"), TEXT("relocate"), TEXT("path"), TEXT("redirect"), TEXT("package") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("delete_asset") };
}

FCapabilityResult FRenameAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		const FString OldPath = A.Str(TEXT("assetPath"));
		const FString DestAssetPath = A.Str(TEXT("destAssetPath"));

		FText PathErrText;
		if (!FPackageName::IsValidLongPackageName(DestAssetPath, false, &PathErrText))
		{
			OutError = FString::Printf(TEXT("Invalid destAssetPath: %s"), *PathErrText.ToString());
			return;
		}

		if (FPackageName::DoesPackageExist(DestAssetPath))
		{
			OutError = FString::Printf(TEXT("Target already exists: %s"), *DestAssetPath);
			return;
		}

		UObject* Asset = LoadObject<UObject>(nullptr, *OldPath);
		if (!Asset)
		{
			Asset = LoadObject<UObject>(nullptr, *(OldPath + TEXT(".") + FPaths::GetBaseFilename(OldPath)));
		}
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Asset not found: %s"), *OldPath);
			return;
		}

		const FString NewName    = FPaths::GetBaseFilename(DestAssetPath);
		const FString NewPkgPath = FPaths::GetPath(DestAssetPath);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		TArray<FAssetRenameData> RenameData;
		RenameData.Add(FAssetRenameData(Asset, NewPkgPath, NewName));
		const bool bOk = AssetTools.RenameAssets(RenameData);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!bOk)
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Renamefailed: %s -> %s"), *OldPath, *DestAssetPath));
		}
		else
		{
			Entry->SetStringField(TEXT("oldPath"), OldPath);
			Entry->SetStringField(TEXT("destAssetPath"), DestAssetPath);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FRenameAssetCapability)

#endif // WITH_EDITOR
