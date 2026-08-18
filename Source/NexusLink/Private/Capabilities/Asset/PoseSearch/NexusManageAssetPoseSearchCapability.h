// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_POSE_SEARCH

#include "NexusActionCapability.h"

/** manage_asset_pose_search — 管理 PoseSearchDatabase：set_schema/add_tag/remove_tag（UE 5.4+）。 */
class FManageAssetPoseSearchCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};

#endif // WITH_POSE_SEARCH
