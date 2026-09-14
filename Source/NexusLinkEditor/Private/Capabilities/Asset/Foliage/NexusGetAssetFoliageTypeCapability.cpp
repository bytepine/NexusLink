// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Foliage/NexusGetAssetFoliageTypeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "NexusMcpTool.h"

void FGetAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_foliage_type");
	Out.SearchAssetTypes = {TEXT("FoliageType")};
	Out.Description = TEXT("Read FoliageType: mesh/density/radius/AlignToNormal. LandscapeGrassType not included.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("FoliageType asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("grass"), TEXT("density"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("manage_asset_foliage_type"), TEXT("create_asset_foliage_type") };
}

FCapabilityResult FGetAssetFoliageTypeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UFoliageType* Type = FNexusAssetUtils::LoadAssetWithFallback<UFoliageType>(AssetPath);
		if (!Type)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Failed to load FoliageType: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Type->GetName());
		Entry->SetStringField(TEXT("path"), Type->GetPathName());
		Entry->SetStringField(TEXT("class"), Type->GetClass()->GetName());
		Entry->SetNumberField(TEXT("density"), Type->Density);
		Entry->SetNumberField(TEXT("radius"), Type->Radius);
		Entry->SetBoolField(TEXT("alignToNormal"), Type->AlignToNormal);
		if (const UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Type))
		{
			Entry->SetStringField(TEXT("meshPath"), ISM->Mesh ? ISM->Mesh->GetPathName() : FString());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetFoliageTypeCapability)
