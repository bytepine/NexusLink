// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusHostUtils.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "Engine/Engine.h"

bool FNexusHostUtils::IsFullEditorCapabilityHost()
{
#if WITH_EDITOR
	return GIsEditor && !IsRunningDedicatedServer();
#else
	return false;
#endif
}

bool FNexusHostUtils::IsCapabilityVisibleOnHost(const FCapRecord& Record)
{
	return IsCapabilityVisibleOnHost(Record, IsFullEditorCapabilityHost());
}

bool FNexusHostUtils::IsCapabilityVisibleOnHost(const FCapRecord& Record, bool bFullEditorHost)
{
	if (bFullEditorHost)
	{
		return true;
	}
	return Record.Instance->GetHostScope() == ENexusCapabilityHostScope::Runtime;
}
