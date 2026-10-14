// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusCreateAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialParameterCollection.h"
#if WITH_EDITOR
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#endif
#include "Misc/PackageName.h"

void FNexusCreateAssetMaterialParameterCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetMaterialParameterCollection");
	Out.Description = TEXT("Create empty MaterialParameterCollection; add params via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path (/Game/…/MPC_Global)")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("global"), TEXT("Material"), TEXT("shader") };
	Out.RelatedCapabilities = { TEXT("GetAssetMaterialParameterCollection"), TEXT("ManageAssetMaterialParameterCollection") };
	Out.WhenToUse = TEXT("Create MPC; add scalar/vector params via manage");
}

FCapabilityResult FNexusCreateAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("CreateAssetMaterialParameterCollection only available in editor builds");
		return;
#else
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("MaterialParameterCollection already exists: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		const FString AssetName   = FPackageName::GetShortName(AssetPath);
		IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

		UMaterialParameterCollectionFactoryNew* Factory = NewObject<UMaterialParameterCollectionFactoryNew>();
		UObject* NewAsset = AT.CreateAsset(AssetName, PackagePath, UMaterialParameterCollection::StaticClass(), Factory);
		if (!NewAsset)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("MPC Createfailed: %s"), *AssetPath));
			return;
		}

		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(NewAsset);
		OutEntry->SetStringField(TEXT("AssetType"), TEXT("MaterialParameterCollection"));
		OutEntry->SetStringField(TEXT("name"),    MPC->GetName());
		OutEntry->SetStringField(TEXT("Path"),    FNexusAssetUtils::PackagePathOf(MPC));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetMaterialParameterCollectionCapability)
