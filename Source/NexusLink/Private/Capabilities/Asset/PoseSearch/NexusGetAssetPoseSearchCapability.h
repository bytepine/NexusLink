// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_POSE_SEARCH

/** get_asset_pose_search — 读取 PoseSearchDatabase / PoseSearchSchema 概览。 */
class FGetAssetPoseSearchCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_POSE_SEARCH
