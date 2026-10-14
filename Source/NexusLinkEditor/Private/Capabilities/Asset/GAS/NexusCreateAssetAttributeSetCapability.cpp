// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetAttributeSetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetAttributeSet");
	Out.Description = TEXT("Create AttributeSet BP; defaults via manage_as, vars via manage_bp.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("New AS Blueprint package path, e.g. '/Game/GAS/AS_Hero'")))
		.Prop(TEXT("ParentClass"), FNexusSchema::Str(TEXT("Parent class (default AttributeSet)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("Attribute"), TEXT("attributeset"), TEXT("stats"), TEXT("health") };
	Out.RelatedCapabilities = { TEXT("GetAssetAttributeSet"), TEXT("ManageAssetAttributeSet"), TEXT("ManageAssetBlueprint") };
	Out.WhenToUse = TEXT("Create empty AttributeSet BP; add vars via ManageAssetBlueprint");
}

FCapabilityResult FNexusCreateAssetAttributeSetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FString ParentClassName = A.Str(TEXT("ParentClass"), TEXT("AttributeSet"));
		const FNexusAssetEditorUtils::FAssetCreateOutcome Created = FNexusAssetEditorUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UAttributeSet::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBlueprint* NewBP = Cast<UBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("name"),      NewBP->GetName());
		OutEntry->SetStringField(TEXT("Path"),      AssetPath);
		if (NewBP->ParentClass)
		{
			OutEntry->SetStringField(TEXT("ParentClass"), NewBP->ParentClass->GetName());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetAttributeSetCapability)

#endif // WITH_GAS
