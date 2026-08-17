// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperTileMapCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperTileMap.h"
#include "PaperTileSet.h"
#include "NexusMcpTool.h"

void FCreateAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_paper_tile_map");
	Out.Description = TEXT("创建 PaperTileMap。可选 mapWidth/Height、tileWidth/Height、tileSetPath。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("mapWidth"), FNexusSchema::Int(TEXT("地图宽（格，默认引擎默认）")))
		.Prop(TEXT("mapHeight"), FNexusSchema::Int(TEXT("地图高（格）")))
		.Prop(TEXT("tileWidth"), FNexusSchema::Int(TEXT("单格像素宽")))
		.Prop(TEXT("tileHeight"), FNexusSchema::Int(TEXT("单格像素高")))
		.Prop(TEXT("tileSetPath"), FNexusSchema::Str(TEXT("默认 PaperTileSet 路径（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("2d"), TEXT("paper"), TEXT("tileset") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_paper_tile_map"), TEXT("manage_asset_paper_tile_map"), TEXT("search_asset")
	};
	Out.WhenToUse = TEXT("新建 PaperTileMap；改尺寸/图层/格子用 manage");
}

FCapabilityResult FCreateAssetPaperTileMapCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UPaperTileMap>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("PaperTileMap already exists: %s"), *AssetPath));
			return;
		}

		UPaperTileSet* DefaultSet = nullptr;
		FString TileSetPath;
		if (Arguments->TryGetStringField(TEXT("tileSetPath"), TileSetPath) && !TileSetPath.IsEmpty())
		{
			DefaultSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
			if (!DefaultSet)
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("PaperTileSet 未找到: %s"), *TileSetPath));
				return;
			}
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败"));
			return;
		}
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPaperTileMap* Map = NewObject<UPaperTileMap>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Map)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败"));
			return;
		}

		if (Arguments->HasField(TEXT("tileWidth")))
		{
			Map->TileWidth = FMath::Max(1, static_cast<int32>(Arguments->GetNumberField(TEXT("tileWidth"))));
		}
		if (Arguments->HasField(TEXT("tileHeight")))
		{
			Map->TileHeight = FMath::Max(1, static_cast<int32>(Arguments->GetNumberField(TEXT("tileHeight"))));
		}

		Map->InitializeNewEmptyTileMap(DefaultSet);

		const bool bHasW = Arguments->HasField(TEXT("mapWidth"));
		const bool bHasH = Arguments->HasField(TEXT("mapHeight"));
		if (bHasW || bHasH)
		{
			const int32 W = bHasW
				? FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("mapWidth"))), 1, 1024)
				: Map->MapWidth;
			const int32 H = bHasH
				? FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("mapHeight"))), 1, 1024)
				: Map->MapHeight;
			Map->ResizeMap(W, H, /*bForceResize=*/true);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Package, Map, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Map->GetName());
		Entry->SetStringField(TEXT("path"), Map->GetPathName());
		Entry->SetNumberField(TEXT("mapWidth"), Map->MapWidth);
		Entry->SetNumberField(TEXT("mapHeight"), Map->MapHeight);
		Entry->SetNumberField(TEXT("tileWidth"), Map->TileWidth);
		Entry->SetNumberField(TEXT("tileHeight"), Map->TileHeight);
		Entry->SetNumberField(TEXT("layerCount"), Map->TileLayers.Num());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPaperTileMapCapability)
#endif
