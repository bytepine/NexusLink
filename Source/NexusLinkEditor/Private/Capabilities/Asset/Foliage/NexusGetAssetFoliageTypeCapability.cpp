// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Foliage/NexusGetAssetFoliageTypeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "NexusMcpTool.h"

void FNexusGetAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetFoliageType");
	Out.SearchAssetTypes = {TEXT("FoliageType")};
	Out.Description = TEXT("Read FoliageType: mesh/density/radius/AlignToNormal. LandscapeGrassType not included.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("FoliageType asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("grass"), TEXT("Density"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("ManageAssetFoliageType"), TEXT("CreateAssetFoliageType") };
}

FCapabilityResult FNexusGetAssetFoliageTypeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UFoliageType* Type = FNexusAssetUtils::LoadAssetWithFallback<UFoliageType>(AssetPath);
		if (!Type)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load FoliageType: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Type->GetName());
		Entry->SetStringField(TEXT("Path"), Type->GetPathName());
		Entry->SetStringField(TEXT("Class"), Type->GetClass()->GetName());
		Entry->SetNumberField(TEXT("Density"), Type->Density);
		Entry->SetNumberField(TEXT("Radius"), Type->Radius);
		Entry->SetBoolField(TEXT("bAlignToNormal"), Type->AlignToNormal);
		if (const UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Type))
		{
			Entry->SetStringField(TEXT("MeshPath"), ISM->Mesh ? ISM->Mesh->GetPathName() : FString());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetFoliageTypeCapability)
