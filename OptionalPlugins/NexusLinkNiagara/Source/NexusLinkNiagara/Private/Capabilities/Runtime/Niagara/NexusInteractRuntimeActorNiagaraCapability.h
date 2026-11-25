// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_NIAGARA
#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

/** InteractRuntimeActorNiagara：PIE 激活/关闭 Niagara 组件。 */
class FNexusInteractRuntimeActorNiagaraCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
