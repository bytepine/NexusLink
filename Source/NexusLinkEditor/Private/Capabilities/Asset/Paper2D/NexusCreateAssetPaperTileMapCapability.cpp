// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperTileMapCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PaperTileMap.h"
#include "PaperTileSet.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetPaperTileMap");
	Out.Description = TEXT("Create PaperTileMap. optional MapWidth/Height, TileWidth/Height, TileSetPath.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("MapWidth"), FNexusSchema::Int(TEXT("Map width (cells, engine default)")))
		.Prop(TEXT("MapHeight"), FNexusSchema::Int(TEXT("Map height (cells)")))
		.Prop(TEXT("TileWidth"), FNexusSchema::Int(TEXT("Tile pixel width")))
		.Prop(TEXT("TileHeight"), FNexusSchema::Int(TEXT("Tile pixel height")))
		.Prop(TEXT("TileSetPath"), FNexusSchema::Str(TEXT("default PaperTileSet path (optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("2d"), TEXT("paper"), TEXT("tileset") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetPaperTileMap"), TEXT("ManageAssetPaperTileMap"), TEXT("SearchAsset")
	};
	Out.WhenToUse = TEXT("Create PaperTileMap; edit size/layers/cells via manage");
}

FCapabilityResult FNexusCreateAssetPaperTileMapCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		if (LoadObject<UPaperTileMap>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("PaperTileMap already exists: %s"), *AssetPath));
			return;
		}

		UPaperTileSet* DefaultSet = nullptr;
		FString TileSetPath;
		if (Arguments->TryGetStringField(TEXT("TileSetPath"), TileSetPath) && !TileSetPath.IsEmpty())
		{
			DefaultSet = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileSet>(TileSetPath);
			if (!DefaultSet)
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("PaperTileSet not found: %s"), *TileSetPath));
				return;
			}
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package"));
			return;
		}
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPaperTileMap* Map = NewObject<UPaperTileMap>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Map)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Creation failed"));
			return;
		}

		if (Arguments->HasField(TEXT("TileWidth")))
		{
			Map->TileWidth = FMath::Max(1, static_cast<int32>(A.Num(TEXT("TileWidth"))));
		}
		if (Arguments->HasField(TEXT("TileHeight")))
		{
			Map->TileHeight = FMath::Max(1, static_cast<int32>(A.Num(TEXT("TileHeight"))));
		}

		Map->InitializeNewEmptyTileMap(DefaultSet);

		const bool bHasW = Arguments->HasField(TEXT("MapWidth"));
		const bool bHasH = Arguments->HasField(TEXT("MapHeight"));
		if (bHasW || bHasH)
		{
			const int32 W = bHasW
				? FMath::Clamp(static_cast<int32>(A.Num(TEXT("MapWidth"))), 1, 1024)
				: Map->MapWidth;
			const int32 H = bHasH
				? FMath::Clamp(static_cast<int32>(A.Num(TEXT("MapHeight"))), 1, 1024)
				: Map->MapHeight;
			Map->ResizeMap(W, H, /*bForceResize=*/true);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Package, Map, AssetPath);
		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(Map);
		Entry->SetNumberField(TEXT("MapWidth"), Map->MapWidth);
		Entry->SetNumberField(TEXT("MapHeight"), Map->MapHeight);
		Entry->SetNumberField(TEXT("TileWidth"), Map->TileWidth);
		Entry->SetNumberField(TEXT("TileHeight"), Map->TileHeight);
		Entry->SetNumberField(TEXT("LayerCount"), Map->TileLayers.Num());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetPaperTileMapCapability)
#endif
