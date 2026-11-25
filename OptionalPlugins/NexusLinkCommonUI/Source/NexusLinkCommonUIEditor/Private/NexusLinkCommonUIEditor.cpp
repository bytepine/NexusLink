// Copyright byteyang. All Rights Reserved.

#include "NexusLink.h"

#if WITH_NEXUS_COMMON_UI_DEBUGGING
#include "Utils/NexusCommonUIDiagnostics.h"
#endif

class FNexusLinkCommonUIEditorModule final : public IModuleInterface
{
    void StartupModule() override
    {
#if WITH_NEXUS_COMMON_UI_DEBUGGING
        StopHandle = FNexusLinkModule::OnServerStopped.AddStatic(&FNexusCommonUIDiagnostics::Shutdown);
#endif
    }

    void ShutdownModule() override
    {
#if WITH_NEXUS_COMMON_UI_DEBUGGING
        FNexusLinkModule::OnServerStopped.Remove(StopHandle);
        FNexusCommonUIDiagnostics::Shutdown();
#endif
    }

#if WITH_NEXUS_COMMON_UI_DEBUGGING
    FDelegateHandle StopHandle;
#endif
};

IMPLEMENT_MODULE(FNexusLinkCommonUIEditorModule, NexusLinkCommonUIEditor)
