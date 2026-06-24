// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusCapability.h"

/** manage_asset_gameplay_ability — 修改 GA 策略/Tag/CostCooldown。 */
class FManageAssetGameplayAbilityCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_GAS
