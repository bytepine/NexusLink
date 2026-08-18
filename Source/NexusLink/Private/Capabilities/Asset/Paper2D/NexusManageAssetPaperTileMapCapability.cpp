// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperTileMapCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "PaperTileMap.h"
#include "PaperTileLayer.h"
#include "PaperTileSet.h"
#include "NexusMcpTool.h"

void FManageAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_paper_tile_map");
	Out.SearchAssetTypes = {TEXT("PaperTileMap")};
	Out.Description = TEXT("Batch edit PaperTileMap: size/TileSet/layers/cells.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("set_map_size"), TEXT("set_tile_size"), TEXT("set_tileset"),
			TEXT("add_layer"), TEXT("remove_layer"), TEXT("set_layer_name"),
			TEXT("set_cell"), TEXT("clear_cell")
		}))
		.Prop(TEXT("mapWidth"), FNexusSchema::Int(TEXT("Map width in cells (set_map_size)")))
		.Prop(TEXT("mapHeight"), FNexusSchema::Int(TEXT("Map height in cells (set_map_size)")))
		.Prop(TEXT("tileWidth"), FNexusSchema::Int(TEXT("Tile pixel width (set_tile_size)")))
		.Prop(TEXT("tileHeight"), FNexusSchema::Int(TEXT("Tile pixel height (set_tile_size)")))
		.Prop(TEXT("tileSetPath"), FNexusSchema::Str(TEXT("PaperTileSet path (set_tileset / set_cell optional)")))
		.Prop(TEXT("layerIndex"), FNexusSchema::Int(TEXT("Layer index (remove/set_layer_name/set_cell/clear_cell)")))
		.Prop(TEXT("layerName"), FNexusSchema::Str(TEXT("Layer name (add_layer optional / set_layer_name)")))
		.Prop(TEXT("x"), FNexusSchema::Int(TEXT("Cell X (set_cell/clear_cell)")))
		.Prop(TEXT("y"), FNexusSchema::Int(TEXT("Cell Y (set_cell/clear_cell)")))
		.Prop(TEXT("tileIndex"), FNexusSchema::Int(TEXT("Tile index (set_cell)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperTileMap asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("layer"), TEXT("cell"), TEXT("tileset"), TEXT("2d") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_paper_tile_map"), TEXT("create_asset_paper_tile_map"), TEXT("save_asset")
	};
	Out.WhenToUse = TEXT("Edit PaperTileMap size/layers/cells; read via get_asset_paper_tile_map");
}

FCapabilityResult FManageAssetPaperTileMapCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Action = Action.ToLower();

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("set_map_size"))
			{
				if (!Op->HasField(TEXT("mapWidth")) || !Op->HasField(TEXT("mapHeight")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_map_size requires mapWidth and mapHeight"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const int32 W = FMath::Clamp(static_cast<int32>(Op->GetNumberField(TEXT("mapWidth"))), 1, 1024);
				const int32 H = FMath::Clamp(static_cast<int32>(Op->GetNumberField(TEXT("mapHeight"))), 1, 1024);
				Map->ResizeMap(W, H, /*bForceResize=*/true);
				bDirty = true;
				Entry->SetNumberField(TEXT("mapWidth"), Map->MapWidth);
				Entry->SetNumberField(TEXT("mapHeight"), Map->MapHeight);
			}
			else if (Action == TEXT("set_tile_size"))
			{
				if (!Op->HasField(TEXT("tileWidth")) && !Op->HasField(TEXT("tileHeight")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_tile_size requires tileWidth and/or tileHeight"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (Op->HasField(TEXT("tileWidth")))
				{
					Map->TileWidth = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("tileWidth"))));
				}
				if (Op->HasField(TEXT("tileHeight")))
				{
					Map->TileHeight = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("tileHeight"))));
				}
				bDirty = true;
				Entry->SetNumberField(TEXT("tileWidth"), Map->TileWidth);
				Entry->SetNumberField(TEXT("tileHeight"), Map->TileHeight);
			}
			else if (Action == TEXT("set_tileset"))
			{
				FString TileSetPath;
				if (!Op->TryGetStringField(TEXT("tileSetPath"), TileSetPath) || TileSetPath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_tileset requires tileSetPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UPaperTileSet* TileSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
				if (!TileSet)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("PaperTileSet not found: %s"), *TileSetPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Map->SelectedTileSet = TileSet;
				bDirty = true;
				Entry->SetStringField(TEXT("tileSet"), TileSet->GetPathName());
			}
			else if (Action == TEXT("add_layer"))
			{
				UPaperTileLayer* Layer = Map->AddNewLayer();
				if (!Layer)
				{
					Entry->SetStringField(TEXT("error"), TEXT("add_layer failed"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString LayerName;
				if (Op->TryGetStringField(TEXT("layerName"), LayerName) && !LayerName.IsEmpty())
				{
					Layer->LayerName = FText::FromString(LayerName);
				}
				bDirty = true;
				Entry->SetNumberField(TEXT("layerIndex"), Layer->GetLayerIndex());
				Entry->SetStringField(TEXT("layerName"), Layer->LayerName.ToString());
				Entry->SetNumberField(TEXT("layerCount"), Map->TileLayers.Num());
			}
			else if (Action == TEXT("remove_layer"))
			{
				if (!Op->HasField(TEXT("layerIndex")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("remove_layer requires layerIndex"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("layerIndex")));
				if (!Map->TileLayers.IsValidIndex(Idx))
				{
					Entry->SetStringField(TEXT("error"), TEXT("layerIndex out of bounds"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (Map->TileLayers.Num() <= 1)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Must keep at least one layer; cannot delete"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UPaperTileLayer* Layer = Map->TileLayers[Idx];
				Map->TileLayers.RemoveAt(Idx);
				if (Layer)
				{
#if NX_UE_HAS_MARK_AS_GARBAGE
					Layer->MarkAsGarbage();
#else
					Layer->MarkPendingKill();
#endif
				}
				bDirty = true;
				Entry->SetNumberField(TEXT("removedIndex"), Idx);
				Entry->SetNumberField(TEXT("layerCount"), Map->TileLayers.Num());
			}
			else if (Action == TEXT("set_layer_name"))
			{
				if (!Op->HasField(TEXT("layerIndex")) || !Op->HasField(TEXT("layerName")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_layer_name requires layerIndex and layerName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("layerIndex")));
				if (!Map->TileLayers.IsValidIndex(Idx) || !Map->TileLayers[Idx])
				{
					Entry->SetStringField(TEXT("error"), TEXT("Invalid layerIndex"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const FString LayerName = Op->GetStringField(TEXT("layerName"));
				Map->TileLayers[Idx]->LayerName = FText::FromString(LayerName);
				bDirty = true;
				Entry->SetNumberField(TEXT("layerIndex"), Idx);
				Entry->SetStringField(TEXT("layerName"), LayerName);
			}
			else if (Action == TEXT("set_cell") || Action == TEXT("clear_cell"))
			{
				if (!Op->HasField(TEXT("layerIndex")) || !Op->HasField(TEXT("x")) || !Op->HasField(TEXT("y")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_cell/clear_cell requires layerIndex、x、y"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const int32 LayerIdx = static_cast<int32>(Op->GetNumberField(TEXT("layerIndex")));
				const int32 X = static_cast<int32>(Op->GetNumberField(TEXT("x")));
				const int32 Y = static_cast<int32>(Op->GetNumberField(TEXT("y")));
				if (!Map->TileLayers.IsValidIndex(LayerIdx) || !Map->TileLayers[LayerIdx])
				{
					Entry->SetStringField(TEXT("error"), TEXT("Invalid layerIndex"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UPaperTileLayer* Layer = Map->TileLayers[LayerIdx];
				if (!Layer->InBounds(X, Y))
				{
					Entry->SetStringField(TEXT("error"), TEXT("Cell coordinates out of bounds"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				FPaperTileInfo Info;
				if (Action == TEXT("set_cell"))
				{
					if (!Op->HasField(TEXT("tileIndex")))
					{
						Entry->SetStringField(TEXT("error"), TEXT("set_cell requires tileIndex"));
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						continue;
					}
					UPaperTileSet* TileSet = nullptr;
					FString TileSetPath;
					if (Op->TryGetStringField(TEXT("tileSetPath"), TileSetPath) && !TileSetPath.IsEmpty())
					{
						TileSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
					}
					else
					{
						TileSet = Map->SelectedTileSet.LoadSynchronous();
					}
					if (!TileSet)
					{
						Entry->SetStringField(TEXT("error"), TEXT("set_cell requires valid TileSet (tileSetPath or SelectedTileSet)"));
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						continue;
					}
					Info.TileSet = TileSet;
					Info.PackedTileIndex = static_cast<int32>(Op->GetNumberField(TEXT("tileIndex")));
					Entry->SetNumberField(TEXT("tileIndex"), Info.GetTileIndex());
					Entry->SetStringField(TEXT("tileSet"), TileSet->GetPathName());
				}

				Layer->SetCell(X, Y, Info);
				bDirty = true;
				Entry->SetNumberField(TEXT("layerIndex"), LayerIdx);
				Entry->SetNumberField(TEXT("x"), X);
				Entry->SetNumberField(TEXT("y"), Y);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		if (bDirty)
		{
			Map->MarkPackageDirty();
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPaperTileMapCapability)
#endif
