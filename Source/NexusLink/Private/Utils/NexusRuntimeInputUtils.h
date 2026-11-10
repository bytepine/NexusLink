// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** 本服务注入的输入按操作拥有，不停止其他来源的 Action 注入。 */
struct FNexusRuntimeInputUtils final
{
	static FCapabilityResult Apply(const TSharedPtr<FJsonObject>& Arguments);
	static FCapabilityResult Query(const TSharedPtr<FJsonObject>& Arguments, bool bRelease);
	static void Shutdown();
};
