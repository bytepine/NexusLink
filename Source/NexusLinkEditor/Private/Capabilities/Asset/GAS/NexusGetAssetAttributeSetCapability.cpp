// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusGetAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

void FGetAssetAttributeSetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_attribute_set");
	Out.SearchAssetTypes = {TEXT("AttributeSet")};
	Out.Description = TEXT("Read all FGameplayAttributeData defaults in AttributeSet BP; read-only.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("AttributeSet Blueprint path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("attribute"), TEXT("health"), TEXT("mana"), TEXT("stamina") };
	Out.RelatedCapabilities = { TEXT("manage_asset_attribute_set"), TEXT("create_asset_attribute_set") };
	Out.WhenToUse = TEXT("Read AttributeSet default property values; no writes");
}

FCapabilityResult FGetAssetAttributeSetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		FString LoadError;
		UBlueprint* BP = FNexusGasUtils::LoadAttributeSetBlueprint(AssetPath, LoadError);
		if (!BP) { OutError = LoadError; return; }
		if (!BP->GeneratedClass) { OutError = TEXT("Blueprint not compiled"); return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		TArray<TSharedPtr<FJsonValue>> Attrs = FNexusGasUtils::SerializeGameplayAttributes(BP->GeneratedClass, CDO);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("path"), AssetPath);
		OutEntry->SetStringField(TEXT("name"),      BP->GetName());
		if (BP->ParentClass) OutEntry->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());
		OutEntry->SetArrayField(TEXT("attributes"),  Attrs);
		OutEntry->SetNumberField(TEXT("count"),      Attrs.Num());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetAttributeSetCapability)

#endif // WITH_GAS
