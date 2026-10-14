// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

/** InteractRuntimeActorAudio：PIE 播放 SoundCue/SoundWave。 */
class FNexusInteractRuntimeActorAudioCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
