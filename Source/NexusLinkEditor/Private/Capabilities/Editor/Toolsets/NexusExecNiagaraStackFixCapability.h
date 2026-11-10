// Copyright byteyang. All Rights Reserved.
#pragma once
#include "Utils/NexusVersionCompat.h"
#if WITH_NIAGARA && NX_UE_HAS_NIAGARA_EXTERNAL_EDIT
#include "NexusCapability.h"
class FNexusExecNiagaraStackFixCapability : public FNexusCapability
{
protected:
	void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
