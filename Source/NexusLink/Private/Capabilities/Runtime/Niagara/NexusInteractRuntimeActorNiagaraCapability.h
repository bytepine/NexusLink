// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_NIAGARA
#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

/** interact_runtime_actor_niagara：PIE 激活/关闭 Niagara 组件。 */
class FInteractRuntimeActorNiagaraCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
