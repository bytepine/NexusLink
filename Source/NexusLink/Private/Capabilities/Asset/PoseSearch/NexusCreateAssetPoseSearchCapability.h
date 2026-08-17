// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#if WITH_POSE_SEARCH
#include "NexusCapability.h"

/** create_asset_pose_search：创建 PoseSearchDatabase 或 PoseSearchSchema。 */
class FCreateAssetPoseSearchCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
