// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * 运行时 Capability 基类——标记 PIE 等运行时能力（自动补 runtime 分类标签）。
 *
 * 主模块 Type=Runtime，但 Startup/Shutdown 在 !WITH_EDITOR 时空操作；本基类区分资产/编辑器能力与 PIE 运行时能力。
 * 新增 PIE 运行时能力：继承本类（或 FNexusRuntimeMultiSectionCapability）即可。
 */
class NEXUSLINK_API FNexusRuntimeCapability : public FNexusCapability
{
public:
	virtual ENexusCapabilityHostScope GetHostScope() const override
	{
		return ENexusCapabilityHostScope::Runtime;
	}
};
