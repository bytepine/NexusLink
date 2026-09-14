// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FGetAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_physical_material");
	Out.SearchAssetTypes = {TEXT("PhysicalMaterial")};
	Out.Description = TEXT("Read PhysicalMaterial: friction/restitution/density/surface type.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PhysicalMaterial asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("material"), TEXT("friction"), TEXT("restitution"), TEXT("surface") };
	Out.RelatedCapabilities = { TEXT("manage_asset_physical_material"), TEXT("create_asset_physical_material") };
}

FCapabilityResult FGetAssetPhysicalMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		UPhysicalMaterial* PM = LoadObject<UPhysicalMaterial>(nullptr, *AssetPath);
		if (!PM)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load PhysicalMaterial: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             PM->GetName());
		Entry->SetStringField(TEXT("path"),             PM->GetPathName());
		Entry->SetNumberField(TEXT("friction"),         PM->Friction);
		Entry->SetNumberField(TEXT("restitution"),      PM->Restitution);
		Entry->SetNumberField(TEXT("density"),          PM->Density);
		Entry->SetNumberField(TEXT("raiseMassToPower"), PM->RaiseMassToPower);
		Entry->SetNumberField(TEXT("surfaceType"),      (double)(int32)PM->SurfaceType.GetValue());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPhysicalMaterialCapability)
