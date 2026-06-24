// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** 读取 BlackboardData 资产 Keys（可通过 BehaviorTree 路径或 Blackboard 资产路径）。工具名 get_asset_blackboard。 */
class FGetAssetBlackboardCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
