// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPortUtilsImpl.h"
#include "NexusEditorServices.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	void ImplOpenSettingsPanel()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule)
		{
			SettingsModule->ShowViewer(TEXT("Editor"), TEXT("Plugins"), TEXT("NexusLink"));
		}
	}
}

void NexusInstallPortUtilsHooks()
{
	FNexusEditorServices::OpenSettingsPanel = &ImplOpenSettingsPanel;
}

#else // !WITH_EDITOR

void NexusInstallPortUtilsHooks() {}

#endif // WITH_EDITOR
