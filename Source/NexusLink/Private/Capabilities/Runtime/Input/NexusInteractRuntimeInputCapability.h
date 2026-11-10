// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusRuntimeCapability.h"
class FNexusInteractRuntimeInputCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
