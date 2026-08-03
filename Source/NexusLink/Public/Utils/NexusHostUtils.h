// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FCapRecord;

/**
 * 宿主门控：非完整 Editor 宿主仅暴露 Runtime Capability。
 * Shipping 由 StartupModule 直接空返回（本工具不再管 Shipping）。
 * 扩展新 Capability：继承 FNexusRuntimeCapability 即可，无需调用本工具。
 */
struct NEXUSLINK_API FNexusHostUtils
{
	/**
	 * 完整 Editor 宿主：带编辑器 UI 的会话（非 Dedicated Server）。
	 * UE4Editor -server / 纯 Game/Server（WITH_EDITOR=0）均为 false。
	 */
	static bool IsFullEditorCapabilityHost();

	/** 当前宿主是否可发现/调用该 Capability（看 GetHostScope，不看 Tags）。 */
	static bool IsCapabilityVisibleOnHost(const FCapRecord& Record);
};
