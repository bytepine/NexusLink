// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_IK_RIG

/** manage_asset_ik_retargeter — 设置源/目标 IKRig、更新 Chain Mapping。 */
class FManageAssetIKRetargeterCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_IK_RIG
