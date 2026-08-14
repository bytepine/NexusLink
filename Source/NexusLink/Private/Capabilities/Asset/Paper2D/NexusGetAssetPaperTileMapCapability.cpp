// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusGetAssetPaperTileMapCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperTileMap.h"
#include "PaperTileSet.h"
#include "NexusMcpTool.h"

void FGetAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_paper_tile_map");
	Out.SearchAssetTypes = {TEXT("PaperTileMap")};
	Out.Description = TEXT("读取 PaperTileMap：尺寸 / 图层数 / 图块集。写 API 本批不收录。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperTileMap 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("2d"), TEXT("tileset") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_sprite"), TEXT("get_asset_paper_flipbook") };
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
				FString::Printf(TEXT("加载 PaperTileMap 失败: %s"), *AssetPath));
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
		Entry->SetStringField(TEXT("tileSet"), Map->SelectedTileSet ? Map->SelectedTileSet->GetPathName() : FString());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPaperTileMapCapability)
#endif
