// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GeometryCollection/NexusGetAssetGeometryCollectionCapability.h"
#if WITH_GEOMETRY_COLLECTION
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "NexusMcpTool.h"

void FGetAssetGeometryCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_geometry_collection");
	Out.SearchAssetTypes = {TEXT("GeometryCollection")};
	Out.Description = TEXT("读取 GeometryCollection：伤害阈值 / 类名摘要。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("GeometryCollection 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("chaos"), TEXT("damage"), TEXT("cluster") };
	Out.RelatedCapabilities = { TEXT("manage_asset_geometry_collection"), TEXT("create_asset_geometry_collection") };
}

FCapabilityResult FGetAssetGeometryCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UGeometryCollection* GC = FNexusAssetUtils::LoadAssetWithFallback<UGeometryCollection>(AssetPath);
		if (!GC)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 GeometryCollection 失败: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), GC->GetName());
		Entry->SetStringField(TEXT("path"), GC->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Thresholds;
		for (float V : GC->DamageThreshold)
		{
			Thresholds.Add(MakeShared<FJsonValueNumber>(V));
		}
		Entry->SetArrayField(TEXT("damageThreshold"), Thresholds);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetGeometryCollectionCapability)
#endif
