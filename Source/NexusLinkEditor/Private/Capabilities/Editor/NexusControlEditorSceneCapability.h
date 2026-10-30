// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"
class FNexusControlEditorSceneCapability : public FNexusCapability
{
protected:
    void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
    FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Args) const override;
};
