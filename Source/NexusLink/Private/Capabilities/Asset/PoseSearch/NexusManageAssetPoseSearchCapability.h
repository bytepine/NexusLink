// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_POSE_SEARCH

/** manage_asset_pose_search — 管理 PoseSearchDatabase：set_schema/add_tag/remove_tag（UE 5.4+）。 */
class FManageAssetPoseSearchCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_POSE_SEARCH
