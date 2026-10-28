// Copyright byteyang. All Rights Reserved.
#pragma once
#if WITH_NEXUS_TOOLSETS
#include "NexusCapability.h"
class FNexusExecNiagaraStackFixCapability : public FNexusCapability
{
protected:
	void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
