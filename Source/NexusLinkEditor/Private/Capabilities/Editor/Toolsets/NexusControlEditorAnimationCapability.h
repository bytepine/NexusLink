// Copyright byteyang. All Rights Reserved.
#pragma once
#if WITH_EDITOR
#include "NexusCapability.h"
class FNexusControlEditorAnimationCapability : public FNexusCapability
{
protected:
    void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
    FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
