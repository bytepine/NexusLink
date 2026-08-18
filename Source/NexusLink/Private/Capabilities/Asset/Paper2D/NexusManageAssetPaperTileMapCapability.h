// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_PAPER2D
#include "NexusActionCapability.h"
/** manage_asset_paper_tile_map — 编辑尺寸 / TileSet / 图层 / 格子。 */
class FManageAssetPaperTileMapCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};
#endif
