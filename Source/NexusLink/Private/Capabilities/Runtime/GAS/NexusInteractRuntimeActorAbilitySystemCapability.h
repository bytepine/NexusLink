// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

/**
 * InteractRuntimeActorAbilitySystem — PIE 运行时对 Actor ASC 执行写操作。
 * Action=ActivateAbility|CancelAbility|ApplyEffect|RemoveEffect|SetAttribute。
 */
class FNexusInteractRuntimeActorAbilitySystemCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_GAS
