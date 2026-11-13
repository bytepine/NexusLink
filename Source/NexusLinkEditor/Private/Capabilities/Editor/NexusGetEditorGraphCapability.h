// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"

class FNexusGetEditorGraphCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
