// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_IK_RIG

/** get_asset_ik_retargeter — 读取 IKRetargeter：源/目标 IKRig 及 Chain Mapping。 */
class FGetAssetIKRetargeterCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_IK_RIG
