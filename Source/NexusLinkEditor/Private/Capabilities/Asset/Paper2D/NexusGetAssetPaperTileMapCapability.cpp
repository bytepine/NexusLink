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

void FNexusGetAssetPaperTileMapCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetPaperTileMap");
	Out.SearchAssetTypes = {TEXT("PaperTileMap")};
	Out.Description = TEXT("Read PaperTileMap: size/layers/tilesets. Use manage/CreateAssetPaperTileMap for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PaperTileMap asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("tilemap"), TEXT("2d"), TEXT("tileset"), TEXT("layer") };
	Out.RelatedCapabilities = {
		TEXT("ManageAssetPaperTileMap"), TEXT("CreateAssetPaperTileMap"),
		TEXT("GetAssetPaperSprite"), TEXT("GetAssetPaperFlipbook")
	};
	Out.WhenToUse = TEXT("Read PaperTileMap metadata; use ManageAssetPaperTileMap for writes");
}

FCapabilityResult FNexusGetAssetPaperTileMapCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;
		UPaperTileMap* Map = FNexusAssetUtils::LoadAssetWithFallback<UPaperTileMap>(AssetPath);
		if (!Map)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load PaperTileMap: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Map->GetName());
		Entry->SetStringField(TEXT("Path"), Map->GetPathName());
		Entry->SetNumberField(TEXT("MapWidth"), Map->MapWidth);
		Entry->SetNumberField(TEXT("MapHeight"), Map->MapHeight);
		Entry->SetNumberField(TEXT("TileWidth"), Map->TileWidth);
		Entry->SetNumberField(TEXT("TileHeight"), Map->TileHeight);
		Entry->SetNumberField(TEXT("LayerCount"), Map->TileLayers.Num());
		if (UPaperTileSet* Selected = Map->SelectedTileSet.LoadSynchronous())
		{
			Entry->SetStringField(TEXT("TileSet"), Selected->GetPathName());
		}
		else
		{
			Entry->SetStringField(TEXT("TileSet"), FString());
		}
		TArray<TSharedPtr<FJsonValue>> LayersArr;
		for (int32 i = 0; i < Map->TileLayers.Num(); ++i)
		{
			UPaperTileLayer* Layer = Map->TileLayers[i];
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetNumberField(TEXT("Index"), i);
			L->SetStringField(TEXT("name"), Layer ? Layer->LayerName.ToString() : FString());
			LayersArr.Add(MakeShared<FJsonValueObject>(L));
		}
		Entry->SetArrayField(TEXT("Layers"), LayersArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPaperTileMapCapability)
#endif
