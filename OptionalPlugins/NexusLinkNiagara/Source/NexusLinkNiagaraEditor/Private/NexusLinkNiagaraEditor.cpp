// Copyright byteyang. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusIndependentNiagara.h"
#include "Utils/NexusNiagaraEditorTools.h"

class FNexusLinkNiagaraEditorModule final : public IModuleInterface
{
    void StartupModule() override
    {
#if WITH_NIAGARA
        FNexusEditorDomainTools::Niagara = &FNexusIndependentNiagara::Niagara;
        FNexusEditorDomainTools::NiagaraSchema = &FNexusNiagaraEditorTools::InputSchema;
#endif
    }

    void ShutdownModule() override
    {
#if WITH_NIAGARA
        FNexusEditorDomainTools::Niagara = nullptr;
        FNexusEditorDomainTools::NiagaraSchema = nullptr;
#endif
    }
};

IMPLEMENT_MODULE(FNexusLinkNiagaraEditorModule, NexusLinkNiagaraEditor)
