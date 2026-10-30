// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"
struct FNexusSceneTools final
{
    static FCapabilityResult Execute(bool bWrite, const TSharedPtr<FJsonObject>& Args);
    static TSharedPtr<FJsonObject> Schema(bool bWrite);
};
