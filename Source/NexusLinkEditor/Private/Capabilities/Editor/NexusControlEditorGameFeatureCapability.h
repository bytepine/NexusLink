// Copyright byteyang. All Rights Reserved.
#pragma once
#if WITH_GAME_FEATURES
#include "NexusCapability.h"
class FNexusControlEditorGameFeatureCapability : public FNexusCapability
{
protected:
    void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
    FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
