// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_IK_RIG

/** manage_asset_ik_rig — 编辑 IKRig：设置预览网格 / 切换 Solver 启用状态。 */
class FManageAssetIKRigCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_IK_RIG
