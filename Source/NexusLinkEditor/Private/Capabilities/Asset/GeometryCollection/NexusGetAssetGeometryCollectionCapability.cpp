// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GeometryCollection/NexusGetAssetGeometryCollectionCapability.h"
#if WITH_GEOMETRY_COLLECTION
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "NexusMcpTool.h"

void FNexusGetAssetGeometryCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetGeometryCollection");
	Out.SearchAssetTypes = {TEXT("GeometryCollection")};
	Out.Description = TEXT("Read GeometryCollection: damage threshold/class summary.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("GeometryCollection asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("chaos"), TEXT("damage"), TEXT("cluster") };
	Out.RelatedCapabilities = { TEXT("ManageAssetGeometryCollection"), TEXT("CreateAssetGeometryCollection") };
}

FCapabilityResult FNexusGetAssetGeometryCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;
		UGeometryCollection* GC = FNexusAssetUtils::LoadAssetWithFallback<UGeometryCollection>(AssetPath);
		if (!GC)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load GeometryCollection: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), GC->GetName());
		Entry->SetStringField(TEXT("Path"), GC->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Thresholds;
		for (float V : GC->DamageThreshold)
		{
			Thresholds.Add(MakeShared<FJsonValueNumber>(V));
		}
		Entry->SetArrayField(TEXT("DamageThreshold"), Thresholds);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetGeometryCollectionCapability)
#endif
