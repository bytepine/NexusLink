// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

void FCreateAssetAttributeSetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_attribute_set");
	Out.Description = TEXT("Create AttributeSet BP; defaults via manage_as, vars via manage_bp.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("New AS Blueprint package path, e.g. '/Game/GAS/AS_Hero'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("Parent class (default AttributeSet)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("attribute"), TEXT("attributeset"), TEXT("stats"), TEXT("health") };
	Out.RelatedCapabilities = { TEXT("get_asset_attribute_set"), TEXT("manage_asset_attribute_set"), TEXT("manage_asset_blueprint") };
	Out.WhenToUse = TEXT("Create empty AttributeSet BP; add vars via manage_asset_blueprint");
}

FCapabilityResult FCreateAssetAttributeSetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FString ParentClassName = A.Str(TEXT("parentClass"), TEXT("AttributeSet"));
		const FNexusAssetUtils::FAssetCreateOutcome Created = FNexusAssetUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UAttributeSet::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBlueprint* NewBP = Cast<UBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("name"),      NewBP->GetName());
		OutEntry->SetStringField(TEXT("path"),      AssetPath);
		if (NewBP->ParentClass)
		{
			OutEntry->SetStringField(TEXT("parentClass"), NewBP->ParentClass->GetName());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAttributeSetCapability)

#endif // WITH_GAS
