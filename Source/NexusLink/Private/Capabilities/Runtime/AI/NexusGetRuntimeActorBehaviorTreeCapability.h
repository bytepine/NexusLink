// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** 读取指定 AIController（或首个）上 BehaviorTree + Blackboard 运行时状态。Capability 名：actor_behavior_tree。 */
class FGetRuntimeActorBehaviorTreeCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
