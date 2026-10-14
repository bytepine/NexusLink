// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusCreateAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetPhysicalMaterial");
	Out.Description = TEXT("Create PhysicalMaterial. optional friction/restitution.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("Friction"), FNexusSchema::Num(TEXT("Friction")))
		.Prop(TEXT("Restitution"), FNexusSchema::Num(TEXT("Restitution")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("Material"), TEXT("Friction"), TEXT("physics") };
	Out.RelatedCapabilities = { TEXT("GetAssetPhysicalMaterial"), TEXT("ManageAssetPhysicalMaterial") };
	Out.WhenToUse = TEXT("Create PhysicalMaterial; edit friction/restitution via manage");
}

FCapabilityResult FNexusCreateAssetPhysicalMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UPhysicalMaterial>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UPhysicalMaterial* PM = Cast<UPhysicalMaterial>(Created.Asset);
		if (!PM)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		if (Arguments->HasField(TEXT("Friction"))) PM->Friction = static_cast<float>(A.Num(TEXT("Friction")));
		if (Arguments->HasField(TEXT("Restitution"))) PM->Restitution = static_cast<float>(A.Num(TEXT("Restitution")));
		FNexusAssetUtils::NotifyAndSaveCreated(PM->GetOutermost(), PM, AssetPath);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, PM);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetPhysicalMaterialCapability)
