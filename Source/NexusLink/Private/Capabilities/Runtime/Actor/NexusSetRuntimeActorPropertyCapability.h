// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UWorld;

/**
 * set_property 工具的 Actor 分支 Capability。
 *
 * 命中条件：target=="actor" 或（无 target 且无顶层 assetPath 时）首条 update 有 actorName。
 * Execute 展开 updates[] 数组，每项独立写入。
 */
class FSetRuntimeActorPropertyCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
