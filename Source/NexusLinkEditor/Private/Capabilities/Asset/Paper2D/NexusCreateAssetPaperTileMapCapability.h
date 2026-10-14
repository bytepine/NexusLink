// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_PAPER2D
#include "NexusCapability.h"
/** CreateAssetPaperTileMap — 新建 PaperTileMap（可选尺寸 / TileSet）。 */
class FNexusCreateAssetPaperTileMapCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
