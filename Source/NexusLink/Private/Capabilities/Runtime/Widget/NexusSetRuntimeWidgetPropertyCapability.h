// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * set_property 工具的 Runtime Widget 分支 Capability。
 *
 * 命中条件：target=="widget" 或（无 target 且无顶层 assetPath 时）首条 update 有 widgetName。
 * Execute 展开 updates[] 数组，每项独立写入。
 */
class FSetRuntimeWidgetPropertyCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
