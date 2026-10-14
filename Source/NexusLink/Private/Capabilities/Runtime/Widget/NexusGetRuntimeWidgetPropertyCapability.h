// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

/**
 * get_property 工具的 Runtime Widget 分支 Capability。
 *
 * 命中条件：Target=="Widget" 或（无 actor/asset 字段时）有 WidgetName。
 * Execute 处理单个 widget，展开 PropertyPaths，每个属性生成一条 entry。
 */
class FNexusGetRuntimeWidgetPropertyCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
