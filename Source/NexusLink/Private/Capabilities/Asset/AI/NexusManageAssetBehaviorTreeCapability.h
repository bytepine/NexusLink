// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * manage_asset_behavior_tree 的 Capability — 管理 BehaviorTree 资产的节点树。
 * 修改 RootNode 运行时树；replace_node 额外通过反射把新节点的 NodeInstance 同步进
 * 编辑器可视化 EdGraph 对应节点（见 .cpp 中 SyncEdGraphNodeInstance）。
 * 若 RootNode 是在更早的调用中被改写的（旧节点指针已丢失，无法增量同步），
 * 用 sync_graph 动作按结构位置整体重建 Graph↔RootNode 对应关系（见 .cpp 中
 * RebuildGraphFromRootNode / SyncNodePairRecursive）。
 */
class FManageAssetBehaviorTreeCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
