// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** create_asset_meta_sound_patch：创建 UMetaSoundPatch 可复用子图资产（≥UE5.1 + WITH_METASOUND）。 */
class FCreateAssetMetaSoundPatchCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
