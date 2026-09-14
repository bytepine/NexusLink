// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_STATETREE

#include "NexusCapability.h"

/** get_asset_state_tree — 只读检查 UStateTree 结构快照（Schema/States/Evaluators/参数）。UE 5.5+。 */
class FGetAssetStateTreeCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_STATETREE
