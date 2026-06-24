// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusCapability.h"

/** create_asset_gameplay_ability — 创建 GameplayAbility Blueprint。 */
class FCreateAssetGameplayAbilityCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_GAS
