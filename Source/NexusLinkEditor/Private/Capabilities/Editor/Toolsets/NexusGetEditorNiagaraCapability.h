// Copyright byteyang. All Rights Reserved.
#pragma once
#if WITH_NIAGARA
#include "NexusCapability.h"
class FNexusGetEditorNiagaraCapability : public FNexusCapability
{
protected:
	void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
