// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_CONTROL_RIG

/** get_asset_control_rig — 读取 ControlRig 层级（骨骼 / 控件 / Null）概览。 */
class FGetAssetControlRigCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_CONTROL_RIG
