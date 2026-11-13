// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"

struct FNexusEditorGraphTools final
{
	static void Define(FNexusCapabilityDefinition& Out, bool bWrite);
	static FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments, bool bWrite);
};
