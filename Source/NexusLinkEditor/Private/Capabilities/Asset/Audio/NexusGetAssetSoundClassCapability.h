// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** get_asset_sound_class：读取 SoundClass 音量/音高/父子关系。 */
class FGetAssetSoundClassCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
