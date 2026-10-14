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

void FNexusManageAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetPaperTileMap");
	Out.SearchAssetTypes = {TEXT("PaperTileMap")};
	Out.Description = TEXT("Batch edit PaperTileMap: size/TileSet/layers/cells.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("SetMapSize"), TEXT("SetTileSize"), TEXT("SetTileset"),
			TEXT("AddLayer"), TEXT("RemoveLayer"), TEXT("SetLayerName"),
			TEXT("SetCell"), TEXT("ClearCell")
		}))
		.Prop(TEXT("MapWidth"), FNexusSchema::Int(TEXT("Map width in cells (SetMapSize)")))
		.Prop(TEXT("MapHeight"), FNexusSchema::Int(TEXT("Map height in cells (SetMapSize)")))
		.Prop(TEXT("TileWidth"), FNexusSchema::Int(TEXT("Tile pixel width (SetTileSize)")))
		.Prop(TEXT("TileHeight"), FNexusSchema::Int(TEXT("Tile pixel height (SetTileSize)")))
		.Prop(TEXT("TileSetPath"), FNexusSchema::Str(TEXT("PaperTileSet path (SetTileset / SetCell optional)")))
		.Prop(TEXT("LayerIndex"), FNexusSchema::Int(TEXT("Layer index (remove/SetLayerName/SetCell/ClearCell)")))
		.Prop(TEXT("LayerName"), FNexusSchema::Str(TEXT("Layer name (AddLayer optional / SetLayerName)")))
		.Prop(TEXT("x"), FNexusSchema::Int(TEXT("Cell X (SetCell/ClearCell)")))
		.Prop(TEXT("y"), FNexusSchema::Int(TEXT("Cell Y (SetCell/ClearCell)")))
		.Prop(TEXT("TileIndex"), FNexusSchema::Int(TEXT("Tile index (SetCell)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PaperTileMap asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("layer"), TEXT("cell"), TEXT("tileset"), TEXT("2d") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetPaperTileMap"), TEXT("CreateAssetPaperTileMap"), TEXT("SaveAsset")
	};
	Out.WhenToUse = TEXT("Edit PaperTileMap size/layers/cells; read via GetAssetPaperTileMap");
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
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return nullptr;
	}
	if (!Op->HasField(TEXT("LayerIndex")) || !Op->HasField(TEXT("x")) || !Op->HasField(TEXT("y")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetCell/ClearCell requires LayerIndex、x、y"));
		return nullptr;
	}
	OutLayerIdx = static_cast<int32>(Op->GetNumberField(TEXT("LayerIndex")));
	OutX = static_cast<int32>(Op->GetNumberField(TEXT("x")));
	OutY = static_cast<int32>(Op->GetNumberField(TEXT("y")));
	if (!Map->TileLayers.IsValidIndex(OutLayerIdx) || !Map->TileLayers[OutLayerIdx])
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Invalid LayerIndex"));
		return nullptr;
	}
	UPaperTileLayer* Layer = Map->TileLayers[OutLayerIdx];
	if (!Layer->InBounds(OutX, OutY))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Cell coordinates out of bounds"));
		return nullptr;
	}
	return Layer;
}

static void HandlePTM_SetMapSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	if (!Op->HasField(TEXT("MapWidth")) || !Op->HasField(TEXT("MapHeight")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetMapSize requires MapWidth and MapHeight"));
		return;
	}
	const int32 W = FMath::Clamp(static_cast<int32>(Op->GetNumberField(TEXT("MapWidth"))), 1, 1024);
	const int32 H = FMath::Clamp(static_cast<int32>(Op->GetNumberField(TEXT("MapHeight"))), 1, 1024);
	Map->ResizeMap(W, H, /*bForceResize=*/true);
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("MapWidth"), Map->MapWidth);
	Ctx.Entry->SetNumberField(TEXT("MapHeight"), Map->MapHeight);
}

static void HandlePTM_SetTileSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	if (!Op->HasField(TEXT("TileWidth")) && !Op->HasField(TEXT("TileHeight")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetTileSize requires TileWidth and/or TileHeight"));
		return;
	}
	if (Op->HasField(TEXT("TileWidth")))
	{
		Map->TileWidth = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("TileWidth"))));
	}
	if (Op->HasField(TEXT("TileHeight")))
	{
		Map->TileHeight = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("TileHeight"))));
	}
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("TileWidth"), Map->TileWidth);
	Ctx.Entry->SetNumberField(TEXT("TileHeight"), Map->TileHeight);
}

static void HandlePTM_SetTileset(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	const FString TileSetPath = FNexusArgs(Op).Str(TEXT("TileSetPath"));
	if (TileSetPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetTileset requires TileSetPath"));
		return;
	}
	UPaperTileSet* TileSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
	if (!TileSet)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("PaperTileSet not found: %s"), *TileSetPath));
		return;
	}
	Map->SelectedTileSet = TileSet;
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("TileSet"), TileSet->GetPathName());
}

static void HandlePTM_AddLayer(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	UPaperTileLayer* Layer = Map->AddNewLayer();
	if (!Layer)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddLayer failed"));
		return;
	}
	const FString LayerName = FNexusArgs(Op).Str(TEXT("LayerName"));
	if (!LayerName.IsEmpty())
	{
		Layer->LayerName = FText::FromString(LayerName);
	}
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("LayerIndex"), Layer->GetLayerIndex());
	Ctx.Entry->SetStringField(TEXT("LayerName"), Layer->LayerName.ToString());
	Ctx.Entry->SetNumberField(TEXT("LayerCount"), Map->TileLayers.Num());
}

static void HandlePTM_RemoveLayer(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	if (!Op->HasField(TEXT("LayerIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveLayer requires LayerIndex"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("LayerIndex")));
	if (!Map->TileLayers.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("LayerIndex out of bounds"));
		return;
	}
	if (Map->TileLayers.Num() <= 1)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Must keep at least one layer; cannot delete"));
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
	Ctx.Entry->SetNumberField(TEXT("RemovedIndex"), Idx);
	Ctx.Entry->SetNumberField(TEXT("LayerCount"), Map->TileLayers.Num());
}

static void HandlePTM_SetLayerName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	if (!Op->HasField(TEXT("LayerIndex")) || !Op->HasField(TEXT("LayerName")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetLayerName requires LayerIndex and LayerName"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("LayerIndex")));
	if (!Map->TileLayers.IsValidIndex(Idx) || !Map->TileLayers[Idx])
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Invalid LayerIndex"));
		return;
	}
	const FString LayerName = Op->GetStringField(TEXT("LayerName"));
	Map->TileLayers[Idx]->LayerName = FText::FromString(LayerName);
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("LayerIndex"), Idx);
	Ctx.Entry->SetStringField(TEXT("LayerName"), LayerName);
}

static void HandlePTM_SetCell(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperTileMap* Map = TileMapFrom(Ctx);
	if (!Map)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperTileMap is null"));
		return;
	}
	int32 LayerIdx = 0, X = 0, Y = 0;
	UPaperTileLayer* Layer = ResolveLayerCell(Op, Ctx, LayerIdx, X, Y);
	if (!Layer) return;
	if (!Op->HasField(TEXT("TileIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetCell requires TileIndex"));
		return;
	}
	UPaperTileSet* TileSet = nullptr;
	const FString TileSetPath = FNexusArgs(Op).Str(TEXT("TileSetPath"));
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
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetCell requires valid TileSet (TileSetPath or SelectedTileSet)"));
		return;
	}
	FPaperTileInfo Info;
	Info.TileSet = TileSet;
	Info.PackedTileIndex = static_cast<int32>(Op->GetNumberField(TEXT("TileIndex")));
	Layer->SetCell(X, Y, Info);
	MarkTileMapDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("TileIndex"), Info.GetTileIndex());
	Ctx.Entry->SetStringField(TEXT("TileSet"), TileSet->GetPathName());
	Ctx.Entry->SetNumberField(TEXT("LayerIndex"), LayerIdx);
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
	Ctx.Entry->SetNumberField(TEXT("LayerIndex"), LayerIdx);
	Ctx.Entry->SetNumberField(TEXT("x"), X);
	Ctx.Entry->SetNumberField(TEXT("y"), Y);
}

bool FNexusManageAssetPaperTileMapCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetPaperTileMapCapability::FinalizeTarget(void* Target) const
{
	FPaperTileMapActionState* State = static_cast<FPaperTileMapActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Map)
	{
		State->Map->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetPaperTileMapCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetMapSize"),   &HandlePTM_SetMapSize);
	OutHandlers.Add(TEXT("SetTileSize"),  &HandlePTM_SetTileSize);
	OutHandlers.Add(TEXT("SetTileset"),    &HandlePTM_SetTileset);
	OutHandlers.Add(TEXT("AddLayer"),      &HandlePTM_AddLayer);
	OutHandlers.Add(TEXT("RemoveLayer"),   &HandlePTM_RemoveLayer);
	OutHandlers.Add(TEXT("SetLayerName"), &HandlePTM_SetLayerName);
	OutHandlers.Add(TEXT("SetCell"),       &HandlePTM_SetCell);
	OutHandlers.Add(TEXT("ClearCell"),     &HandlePTM_ClearCell);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPaperTileMapCapability)
#endif
