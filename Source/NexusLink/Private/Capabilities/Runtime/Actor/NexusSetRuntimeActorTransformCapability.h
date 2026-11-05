// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusRuntimeCapability.h"

/** 用 SetActorLocation / SetActorRotation / SetActorScale3D 改运行时变换，并返回改前的值。 */
class FNexusSetRuntimeActorTransformCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
