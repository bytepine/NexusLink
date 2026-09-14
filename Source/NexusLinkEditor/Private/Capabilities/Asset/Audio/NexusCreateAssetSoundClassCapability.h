// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** create_asset_sound_class：创建 USoundClass 资产。 */
class FCreateAssetSoundClassCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
