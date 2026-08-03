// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * 运行时 Capability 基类——Dedicated Server / 纯 Game 下自动可见。
 *
 * 新增 PIE/Game/DS 能力：继承本类（或 FNexusRuntimeMultiSectionCapability），
 * 按常规写 BuildDefinition/Execute 即可，无需再处理宿主过滤或 Shipping 门控。
 */
class NEXUSLINK_API FNexusRuntimeCapability : public FNexusCapability
{
public:
	virtual ENexusCapabilityHostScope GetHostScope() const override
	{
		return ENexusCapabilityHostScope::Runtime;
	}
};
