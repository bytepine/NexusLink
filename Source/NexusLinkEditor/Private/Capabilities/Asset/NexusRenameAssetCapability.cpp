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

void FNexusRenameAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("RenameAsset");
	Out.Description = TEXT("Move or rename asset. Auto-generates redirectors.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),     FNexusSchema::Str(TEXT("Current asset path")))
		.Prop(TEXT("DestAssetPath"), FNexusSchema::Str(TEXT("Target full asset path")))
		.Required({ TEXT("AssetPath"), TEXT("DestAssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("move"), TEXT("relocate"), TEXT("Path"), TEXT("redirect"), TEXT("package") };
	Out.RelatedCapabilities = { TEXT("SaveAsset"), TEXT("DeleteAsset") };
}

FCapabilityResult FNexusRenameAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		const FString OldPath = A.Str(TEXT("AssetPath"));
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
			Entry->SetBoolField(TEXT("bSuccess"), false);
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Renamefailed: %s -> %s"), *OldPath, *DestAssetPath));
		}
		else
		{
			Entry->SetStringField(TEXT("OldPath"), OldPath);
			Entry->SetStringField(TEXT("DestAssetPath"), DestAssetPath);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusRenameAssetCapability)

#endif // WITH_EDITOR
