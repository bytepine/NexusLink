// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * manage_asset_behavior_tree 的 Capability — 管理 BehaviorTree 资产的节点树。
 * 修改 RootNode 运行时树；编辑器下 PostEditChange 刷新 BT 图（与运行时一致）。
 */
class FManageAssetBehaviorTreeCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
