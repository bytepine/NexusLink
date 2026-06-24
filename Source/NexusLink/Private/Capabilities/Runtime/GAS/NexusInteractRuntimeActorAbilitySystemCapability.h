// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusCapability.h"

/**
 * interact_runtime_actor_ability_system — PIE 运行时对 Actor ASC 执行写操作。
 * action=activate_ability|cancel_ability|apply_effect|remove_effect|set_attribute。
 */
class FInteractRuntimeActorAbilitySystemCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_GAS
