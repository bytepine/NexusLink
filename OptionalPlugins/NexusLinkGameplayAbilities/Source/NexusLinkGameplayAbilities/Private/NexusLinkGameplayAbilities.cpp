// Copyright byteyang. All Rights Reserved.

#include "NexusLink.h"

#if WITH_GAS
#include "Utils/NexusGasDiagnostics.h"
#endif

class FNexusLinkGameplayAbilitiesModule final : public IModuleInterface
{
    void StartupModule() override
    {
#if WITH_GAS
        StopHandle = FNexusLinkModule::OnServerStopped.AddStatic(&FNexusGasDiagnostics::Shutdown);
#endif
    }

    void ShutdownModule() override
    {
#if WITH_GAS
        FNexusLinkModule::OnServerStopped.Remove(StopHandle);
        FNexusGasDiagnostics::Shutdown();
#endif
    }

#if WITH_GAS
    FDelegateHandle StopHandle;
#endif
};

IMPLEMENT_MODULE(FNexusLinkGameplayAbilitiesModule, NexusLinkGameplayAbilities)
