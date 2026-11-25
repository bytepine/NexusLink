// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GeometryCollection/NexusCreateAssetGeometryCollectionCapability.h"
#if WITH_GEOMETRY_COLLECTION
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetGeometryCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetGeometryCollection");
	Out.Description = TEXT("Create empty GeometryCollection. Does not fracture StaticMesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("chaos"), TEXT("fracture"), TEXT("destruction") };
	Out.RelatedCapabilities = { TEXT("GetAssetGeometryCollection"), TEXT("ManageAssetGeometryCollection") };
}

FCapabilityResult FNexusCreateAssetGeometryCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UGeometryCollection>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UGeometryCollection* GC = Cast<UGeometryCollection>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, GC);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetGeometryCollectionCapability)
#endif
