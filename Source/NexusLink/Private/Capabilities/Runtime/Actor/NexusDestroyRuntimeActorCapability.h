// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "GameFramework/Actor.h"

/** destroy_actor 的 Capability —— 销毁单个 Actor（actorName）。*/
class FDestroyRuntimeActorCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
