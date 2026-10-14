// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

class UWorld;

/**
 * SetProperty 工具的 Actor 分支 Capability。
 *
 * 命中条件：Target=="Actor" 或（无 target 且无顶层 AssetPath 时）首条 update 有 ActorName。
 * Execute 展开 updates[] 数组，每项独立写入。
 */
class FNexusSetRuntimeActorPropertyCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
