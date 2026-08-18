// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusGetAssetPaperTileMapCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperTileMap.h"
#include "PaperTileLayer.h"
#include "PaperTileSet.h"
#include "NexusMcpTool.h"

void FGetAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_paper_tile_map");
	Out.SearchAssetTypes = {TEXT("PaperTileMap")};
	Out.Description = TEXT("Read PaperTileMap: size/layers/tilesets. Use manage/create_asset_paper_tile_map for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperTileMap asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("2d"), TEXT("tileset"), TEXT("layer") };
	Out.RelatedCapabilities = {
		TEXT("manage_asset_paper_tile_map"), TEXT("create_asset_paper_tile_map"),
		TEXT("get_asset_paper_sprite"), TEXT("get_asset_paper_flipbook")
	};
	Out.WhenToUse = TEXT("Read PaperTileMap metadata; use manage_asset_paper_tile_map for writes");
}

FCapabilityResult FGetAssetPaperTileMapCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UPaperTileMap* Map = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileMap>(AssetPath);
		if (!Map)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Failed to load PaperTileMap: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Map->GetName());
		Entry->SetStringField(TEXT("path"), Map->GetPathName());
		Entry->SetNumberField(TEXT("mapWidth"), Map->MapWidth);
		Entry->SetNumberField(TEXT("mapHeight"), Map->MapHeight);
		Entry->SetNumberField(TEXT("tileWidth"), Map->TileWidth);
		Entry->SetNumberField(TEXT("tileHeight"), Map->TileHeight);
		Entry->SetNumberField(TEXT("layerCount"), Map->TileLayers.Num());
		if (UPaperTileSet* Selected = Map->SelectedTileSet.LoadSynchronous())
		{
			Entry->SetStringField(TEXT("tileSet"), Selected->GetPathName());
		}
		else
		{
			Entry->SetStringField(TEXT("tileSet"), FString());
		}
		TArray<TSharedPtr<FJsonValue>> LayersArr;
		for (int32 i = 0; i < Map->TileLayers.Num(); ++i)
		{
			UPaperTileLayer* Layer = Map->TileLayers[i];
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetNumberField(TEXT("index"), i);
			L->SetStringField(TEXT("name"), Layer ? Layer->LayerName.ToString() : FString());
			LayersArr.Add(MakeShared<FJsonValueObject>(L));
		}
		Entry->SetArrayField(TEXT("layers"), LayersArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPaperTileMapCapability)
#endif
