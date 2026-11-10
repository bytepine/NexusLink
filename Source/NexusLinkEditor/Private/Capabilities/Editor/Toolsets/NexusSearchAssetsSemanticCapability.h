// Copyright byteyang. All Rights Reserved.
#pragma once
#if WITH_NEXUS_SEMANTIC_SEARCH
#include "NexusCapability.h"
class FNexusSearchAssetsSemanticCapability : public FNexusCapability
{
protected:
	void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
