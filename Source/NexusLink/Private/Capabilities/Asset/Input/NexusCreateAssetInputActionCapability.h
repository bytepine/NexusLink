// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_ENHANCED_INPUT

#include "NexusCapability.h"

/** create_asset_input_action — 创建空白 UInputAction（Enhanced Input，UE5+）。 */
class FCreateAssetInputActionCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_ENHANCED_INPUT
