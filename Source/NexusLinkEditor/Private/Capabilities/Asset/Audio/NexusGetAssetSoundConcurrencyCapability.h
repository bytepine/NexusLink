// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** get_asset_sound_concurrency：读取 SoundConcurrency 属性。 */
class FGetAssetSoundConcurrencyCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
