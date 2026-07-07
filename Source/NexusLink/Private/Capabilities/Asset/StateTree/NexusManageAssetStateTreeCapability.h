// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_STATETREE

#include "NexusCapability.h"

/** manage_asset_state_tree — 编辑 StateTree：add_state/remove_state/rename_state/add_task/recompile。UE 5.5+。 */
class FManageAssetStateTreeCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_STATETREE
