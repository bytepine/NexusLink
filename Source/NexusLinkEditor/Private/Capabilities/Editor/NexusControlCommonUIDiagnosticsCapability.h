// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"
#if WITH_NEXUS_COMMON_UI_DEBUGGING
class FNexusControlCommonUIDiagnosticsCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
