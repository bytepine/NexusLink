// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperTileMapCapability.h"
#if WITH_PAPER2D
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
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

struct FPaperTileMapActionState
{
	UPaperTileMap* Map = nullptr;
	bool bDirty = false;
};

static FPaperTileMapActionState* TileMapState(FNexusActionContext& Ctx)
{
	return static_cast<FPaperTileMapActionState*>(Ctx.Target);
}

static UPaperTileMap* TileMapFrom(FNexusActionContext& Ctx)
{
	FPaperTileMapActionState* S = TileMapState(Ctx);
	return S ? S->Map : nullptr;
}

static void MarkTileMapDirty(FNexusActionContext& Ctx)
{
	if (FPaperTileMapActionState* S = TileMapState(Ctx))
	{
		S->bDirty = true;
	}
}

static UPaperTileLayer* ResolveLayerCell(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, int32& OutLayerIdx, int32& OutX, int32& OutY)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Op->HasField(TEXT("layerIndex")) || !Op->HasField(TEXT("x")) || !Op->HasField(TEXT("y")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_cell/clear_cell requires layerIndex、x、y"));
		return nullptr;
	}
	OutLayerIdx = static_cast<int32>(Op->GetNumberField(TEXT("layerIndex")));
	OutX = static_cast<int32>(Op->GetNumberField(TEXT("x")));
	OutY = static_cast<int32>(Op->GetNumberField(TEXT("y")));
	if (!Map->TileLayers.IsValidIndex(OutLayerIdx) || !Map->TileLayers[OutLayerIdx])
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Invalid layerIndex"));
		return nullptr;
	}
	UPaperTileLayer* Layer = Map->TileLayers[OutLayerIdx];
	if (!Layer->InBounds(OutX, OutY))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Cell coordinates out of bounds"));
		return nullptr;
	}
	return Layer;
}

static void HandlePTM_SetMapSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Op->HasField(TEXT("mapWidth")) || !Op->HasField(TEXT("mapHeight")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_map_size requires mapWidth and mapHeight"));
		return;
	}
	const int32 W = FMath::Clamp(static_cast<int32>(Op->GetNumberField(TEXT("mapWidth"))), 1, 1024);
	const int32 H = FMath::Clamp(static_cast<int32>(Op->GetNumberField(TEXT("mapHeight"))), 1, 1024);
	Map->ResizeMap(W, H, /*bForceResize=*/true);
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("mapWidth"), Map->MapWidth);
	Ctx.Entry->SetNumberField(TEXT("mapHeight"), Map->MapHeight);
}

static void HandlePTM_SetTileSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Op->HasField(TEXT("tileWidth")) && !Op->HasField(TEXT("tileHeight")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_tile_size requires tileWidth and/or tileHeight"));
		return;
	}
	if (Op->HasField(TEXT("tileWidth")))
	{
		Map->TileWidth = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("tileWidth"))));
	}
	if (Op->HasField(TEXT("tileHeight")))
	{
		Map->TileHeight = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("tileHeight"))));
	}
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("tileWidth"), Map->TileWidth);
	Ctx.Entry->SetNumberField(TEXT("tileHeight"), Map->TileHeight);
}

static void HandlePTM_SetTileset(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	const FString TileSetPath = FNexusArgs(Op).Str(TEXT("tileSetPath"));
	if (TileSetPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_tileset requires tileSetPath"));
		return;
	}
	UPaperTileSet* TileSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
	if (!TileSet)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("PaperTileSet not found: %s"), *TileSetPath));
		return;
	}
	Map->SelectedTileSet = TileSet;
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("tileSet"), TileSet->GetPathName());
}

static void HandlePTM_AddLayer(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	UPaperTileLayer* Layer = Map->AddNewLayer();
	if (!Layer)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_layer failed"));
		return;
	}
	const FString LayerName = FNexusArgs(Op).Str(TEXT("layerName"));
	if (!LayerName.IsEmpty())
	{
		Layer->LayerName = FText::FromString(LayerName);
	}
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("layerIndex"), Layer->GetLayerIndex());
	Ctx.Entry->SetStringField(TEXT("layerName"), Layer->LayerName.ToString());
	Ctx.Entry->SetNumberField(TEXT("layerCount"), Map->TileLayers.Num());
}

static void HandlePTM_RemoveLayer(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Op->HasField(TEXT("layerIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_layer requires layerIndex"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("layerIndex")));
	if (!Map->TileLayers.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("layerIndex out of bounds"));
		return;
	}
	if (Map->TileLayers.Num() <= 1)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Must keep at least one layer; cannot delete"));
		return;
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
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("removedIndex"), Idx);
	Ctx.Entry->SetNumberField(TEXT("layerCount"), Map->TileLayers.Num());
}

static void HandlePTM_SetLayerName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Op->HasField(TEXT("layerIndex")) || !Op->HasField(TEXT("layerName")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_layer_name requires layerIndex and layerName"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("layerIndex")));
	if (!Map->TileLayers.IsValidIndex(Idx) || !Map->TileLayers[Idx])
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Invalid layerIndex"));
		return;
	}
	const FString LayerName = Op->GetStringField(TEXT("layerName"));
	Map->TileLayers[Idx]->LayerName = FText::FromString(LayerName);
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("layerIndex"), Idx);
	Ctx.Entry->SetStringField(TEXT("layerName"), LayerName);
}

static void HandlePTM_SetCell(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	int32 LayerIdx = 0, X = 0, Y = 0;
	UPaperTileLayer* Layer = ResolveLayerCell(Op, Ctx, LayerIdx, X, Y);
	if (!Layer) return;
	if (!Op->HasField(TEXT("tileIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_cell requires tileIndex"));
		return;
	}
	UPaperTileSet* TileSet = nullptr;
	const FString TileSetPath = FNexusArgs(Op).Str(TEXT("tileSetPath"));
	if (!TileSetPath.IsEmpty())
	{
		TileSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
	}
	else
	{
		TileSet = Map->SelectedTileSet.LoadSynchronous();
	}
	if (!TileSet)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_cell requires valid TileSet (tileSetPath or SelectedTileSet)"));
		return;
	}
	FPaperTileInfo Info;
	Info.TileSet = TileSet;
	Info.PackedTileIndex = static_cast<int32>(Op->GetNumberField(TEXT("tileIndex")));
	Layer->SetCell(X, Y, Info);
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("tileIndex"), Info.GetTileIndex());
	Ctx.Entry->SetStringField(TEXT("tileSet"), TileSet->GetPathName());
	Ctx.Entry->SetNumberField(TEXT("layerIndex"), LayerIdx);
	Ctx.Entry->SetNumberField(TEXT("x"), X);
	Ctx.Entry->SetNumberField(TEXT("y"), Y);
}

static void HandlePTM_ClearCell(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	int32 LayerIdx = 0, X = 0, Y = 0;
	UPaperTileLayer* Layer = ResolveLayerCell(Op, Ctx, LayerIdx, X, Y);
	if (!Layer) return;
	Layer->SetCell(X, Y, FPaperTileInfo());
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("layerIndex"), LayerIdx);
	Ctx.Entry->SetNumberField(TEXT("x"), X);
	Ctx.Entry->SetNumberField(TEXT("y"), Y);
}

bool FManageAssetPaperTileMapCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UPaperTileMap* Map = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileMap>(AssetPath);
	if (!Map)
	{
		OutError = FString::Printf(TEXT("Failed to load PaperTileMap: %s"), *AssetPath);
		return false;
	}
	FPaperTileMapActionState* State = new FPaperTileMapActionState();
	State->Map = Map;
	OutTarget = State;
	return true;
}

void FManageAssetPaperTileMapCapability::FinalizeTarget(void* Target) const
{
	FPaperTileMapActionState* State = static_cast<FPaperTileMapActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Map)
	{
		State->Map->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetPaperTileMapCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_map_size"),   &HandlePTM_SetMapSize);
	OutHandlers.Add(TEXT("set_tile_size"),  &HandlePTM_SetTileSize);
	OutHandlers.Add(TEXT("set_tileset"),    &HandlePTM_SetTileset);
	OutHandlers.Add(TEXT("add_layer"),      &HandlePTM_AddLayer);
	OutHandlers.Add(TEXT("remove_layer"),   &HandlePTM_RemoveLayer);
	OutHandlers.Add(TEXT("set_layer_name"), &HandlePTM_SetLayerName);
	OutHandlers.Add(TEXT("set_cell"),       &HandlePTM_SetCell);
	OutHandlers.Add(TEXT("clear_cell"),     &HandlePTM_ClearCell);
}

REGISTER_MCP_CAPABILITY(FManageAssetPaperTileMapCapability)
#endif
