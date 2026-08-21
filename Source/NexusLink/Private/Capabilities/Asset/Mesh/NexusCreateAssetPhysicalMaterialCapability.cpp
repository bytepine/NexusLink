// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusCreateAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FCreateAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_physical_material");
	Out.Description = TEXT("Create PhysicalMaterial. optional friction/restitution.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("friction"), FNexusSchema::Num(TEXT("Friction")))
		.Prop(TEXT("restitution"), FNexusSchema::Num(TEXT("Restitution")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("material"), TEXT("friction"), TEXT("physics") };
	Out.RelatedCapabilities = { TEXT("get_asset_physical_material"), TEXT("manage_asset_physical_material") };
	Out.WhenToUse = TEXT("Create PhysicalMaterial; edit friction/restitution via manage");
}

FCapabilityResult FCreateAssetPhysicalMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
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
		if (Arguments->HasField(TEXT("friction"))) PM->Friction = static_cast<float>(A.Num(TEXT("friction")));
		if (Arguments->HasField(TEXT("restitution"))) PM->Restitution = static_cast<float>(A.Num(TEXT("restitution")));
		FNexusAssetUtils::NotifyAndSaveCreated(PM->GetOutermost(), PM, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), PM->GetName());
		Entry->SetStringField(TEXT("path"), PM->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPhysicalMaterialCapability)
