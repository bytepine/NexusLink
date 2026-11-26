// Copyright byteyang. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusIndependentPCG.h"

#if WITH_PCG
void NexusInstallPCGStructure();
void NexusRemovePCGStructure();
void NexusInstallPCGGraphView();
void NexusRemovePCGGraphView();
#endif

class FNexusLinkPCGEditorModule final : public IModuleInterface
{
    void StartupModule() override
    {
#if WITH_PCG
        NexusInstallPCGStructure();
        NexusInstallPCGGraphView();
        FNexusEditorDomainTools::PCG = &FNexusIndependentPCG::PCG;
        FNexusEditorDomainTools::ExecutePCG = &FNexusIndependentPCG::ExecutePCG;
#endif
    }

    void ShutdownModule() override
    {
#if WITH_PCG
        NexusRemovePCGGraphView();
        NexusRemovePCGStructure();
        FNexusEditorDomainTools::PCG = nullptr;
        FNexusEditorDomainTools::ExecutePCG = nullptr;
#endif
    }
};

IMPLEMENT_MODULE(FNexusLinkPCGEditorModule, NexusLinkPCGEditor)
