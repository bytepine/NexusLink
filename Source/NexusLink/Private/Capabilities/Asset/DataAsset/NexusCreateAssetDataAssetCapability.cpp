// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusCreateAssetDataAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/DataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetDataAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_data_asset");
	Out.Description = TEXT("Create typed DataAsset. Requires subclass; non-abstract.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("New DataAsset package path")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("Non-abstract parent class name"), TEXT("PrimaryDataAsset")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dataasset"), TEXT("primarydataasset"), TEXT("config"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("manage_asset_data_asset"), TEXT("get_asset_data_asset") };
	Out.WhenToUse = TEXT("Create DataAsset; parentClass default PrimaryDataAsset");
}

FCapabilityResult FCreateAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();


		const FString AssetPath = A.Str(TEXT("assetPath"));
		FString ParentClassName = TEXT("PrimaryDataAsset");
		if (Arguments->HasField(TEXT("parentClass")))
		{
			ParentClassName = A.Str(TEXT("parentClass"));
		}

		UClass* ParentClass = FNexusAssetUtils::FindClassWithUPrefix(ParentClassName);
		if (!ParentClass || !ParentClass->IsChildOf(UDataAsset::StaticClass()))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("DataAsset subclass not found: %s"), *ParentClassName));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}
		if (ParentClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Class %s is abstract; use non-abstract UDataAsset subclass as parentClass."),
				*ParentClassName));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset(AssetPath, ParentClass);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UDataAsset* NewDA = Cast<UDataAsset>(Created.Asset);

		OutEntry->SetStringField(TEXT("name"), NewDA->GetName());
		OutEntry->SetStringField(TEXT("path"), NewDA->GetOutermost()->GetName());
		OutEntry->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataAssetCapability)
