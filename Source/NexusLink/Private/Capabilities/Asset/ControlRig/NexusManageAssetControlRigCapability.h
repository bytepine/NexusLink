// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_CONTROL_RIG

/** manage_asset_control_rig — 重命名元素 / 设置控件 Shape/Color 等属性。 */
class FManageAssetControlRigCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_CONTROL_RIG
