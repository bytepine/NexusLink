// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * 运行时 Capability 基类——标记 PIE 等运行时能力（自动补 runtime 分类标签）。
 *
 * NexusLink 主模块 Type=Runtime：Editor 二进制与 Development/DebugGame 的独立 Game/DS 均加载；Shipping 编译期整体剔除。
 * 「运行时」既包括编辑器进程内的 PIE / editor-hosted DS，也包括独立打包的 Game / DS 进程。
 * 新增 PIE/Game 运行时能力：继承本类（或 FNexusRuntimeMultiSectionCapability）即可。
 * 落地目录：Source/NexusLink/Private/Capabilities/{Runtime,Lua/Runtime}/<域>/（见 CapabilitySpec.md §2.1.0）。
 */
class NEXUSLINK_API FNexusRuntimeCapability : public FNexusCapability
{
public:
	virtual ENexusCapabilityHostScope GetHostScope() const override
	{
		return ENexusCapabilityHostScope::Runtime;
	}
};
