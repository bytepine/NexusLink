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

void FNexusCreateAssetDataAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetDataAsset");
	Out.Description = TEXT("Create typed DataAsset. Requires subclass; non-abstract.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("New DataAsset package path")))
		.Prop(TEXT("ParentClass"), FNexusSchema::Str(TEXT("Non-abstract parent class name"), TEXT("PrimaryDataAsset")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dataasset"), TEXT("primarydataasset"), TEXT("config"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("ManageAssetDataAsset"), TEXT("GetAssetDataAsset") };
	Out.WhenToUse = TEXT("Create DataAsset; ParentClass default PrimaryDataAsset");
}

FCapabilityResult FNexusCreateAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();


		const FString AssetPath = A.Str(TEXT("AssetPath"));
		FString ParentClassName = TEXT("PrimaryDataAsset");
		if (Arguments->HasField(TEXT("ParentClass")))
		{
			ParentClassName = A.Str(TEXT("ParentClass"));
		}

		UClass* ParentClass = FNexusAssetUtils::FindClassWithUPrefix(ParentClassName);
		if (!ParentClass || !ParentClass->IsChildOf(UDataAsset::StaticClass()))
		{
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("DataAsset subclass not found: %s"), *ParentClassName));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}
		if (ParentClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("Class %s is abstract; use non-abstract UDataAsset subclass as ParentClass."),
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
		OutEntry->SetStringField(TEXT("Path"), NewDA->GetOutermost()->GetName());
		OutEntry->SetStringField(TEXT("ParentClass"), ParentClass->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetDataAssetCapability)
