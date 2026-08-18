// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_STATETREE

#include "NexusActionCapability.h"

/** manage_asset_state_tree — 编辑 StateTree：add_state/remove_state/rename_state/add_task/recompile。UE 5.5+。 */
class FManageAssetStateTreeCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
};

#endif // WITH_STATETREE
