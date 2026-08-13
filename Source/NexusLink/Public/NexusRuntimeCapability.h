// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * 运行时 Capability 基类——标记 PIE 等运行时能力（自动补 runtime 分类标签）。
 *
 * 主模块 Type=Runtime 仅为让 Game/Server 目标可链接；Startup/Shutdown 在 !WITH_EDITOR 时空操作，
 * 且 capability 在 !WITH_EDITOR 下不再自注册，故本基类的「运行时」指编辑器进程内的 PIE /
 * editor-hosted DS，而非独立打包的 Game / Server 进程。
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
