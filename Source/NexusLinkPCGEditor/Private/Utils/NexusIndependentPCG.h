// Copyright byteyang. All Rights Reserved.
#pragma once
#include "Utils/NexusEditorDomainTools.h"

struct FNexusIndependentPCG final
{
    static FNexusEditorDomainTools::FAction PCG;
    static void ExecutePCG(const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation);
};
