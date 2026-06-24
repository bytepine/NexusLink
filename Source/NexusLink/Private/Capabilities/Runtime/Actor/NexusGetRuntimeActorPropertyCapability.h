// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UWorld;

/**
 * get_property 工具的 Actor 分支 Capability。
 *
 * 命中条件：target=="actor" 或（未显式指定 target 时）有 actorName。
 * Execute 处理单个 Actor，生成一条 entry。
 */
class FGetRuntimeActorPropertyCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
