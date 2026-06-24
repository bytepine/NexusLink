// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusCapability.h"

/** manage_asset_gameplay_effect — 批量修改 GE 策略/Tag/Modifier。 */
class FManageAssetGameplayEffectCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_GAS
