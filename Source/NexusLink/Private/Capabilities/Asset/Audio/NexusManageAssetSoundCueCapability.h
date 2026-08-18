// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusActionCapability.h"

/** manage_asset_sound_cue — 编辑 SoundCue 属性（音量/音高/并发）。*/
class FManageAssetSoundCueCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
};
