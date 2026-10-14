// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_STATETREE
#include "NexusCapability.h"

/** CreateAssetStateTree：创建空白 StateTree（UE 5.5+）。 */
class FNexusCreateAssetStateTreeCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
