// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_ENHANCED_INPUT

#include "NexusCapability.h"

/** get_asset_input_action — 读取 UInputAction 的 ValueType / Trigger / Modifier 配置（UE5+）。 */
class FGetAssetInputActionCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_ENHANCED_INPUT
