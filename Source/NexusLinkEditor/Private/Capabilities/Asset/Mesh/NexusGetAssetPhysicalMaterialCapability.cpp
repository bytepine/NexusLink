// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FNexusGetAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetPhysicalMaterial");
	Out.SearchAssetTypes = {TEXT("PhysicalMaterial")};
	Out.Description = TEXT("Read PhysicalMaterial: friction/restitution/density/surface type.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PhysicalMaterial asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("Material"), TEXT("Friction"), TEXT("Restitution"), TEXT("Surface") };
	Out.RelatedCapabilities = { TEXT("ManageAssetPhysicalMaterial"), TEXT("CreateAssetPhysicalMaterial") };
}

FCapabilityResult FNexusGetAssetPhysicalMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		UPhysicalMaterial* PM = LoadObject<UPhysicalMaterial>(nullptr, *AssetPath);
		if (!PM)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load PhysicalMaterial: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             PM->GetName());
		Entry->SetStringField(TEXT("Path"),             PM->GetPathName());
		Entry->SetNumberField(TEXT("Friction"),         PM->Friction);
		Entry->SetNumberField(TEXT("Restitution"),      PM->Restitution);
		Entry->SetNumberField(TEXT("Density"),          PM->Density);
		Entry->SetNumberField(TEXT("RaiseMassToPower"), PM->RaiseMassToPower);
		Entry->SetNumberField(TEXT("SurfaceType"),      (double)(int32)PM->SurfaceType.GetValue());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPhysicalMaterialCapability)
