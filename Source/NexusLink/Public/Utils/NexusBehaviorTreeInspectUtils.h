// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Domain — §7.4 豁免：单 Capability 引用但函数体复杂（递归节点遍历 >30 行）
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBTNode;

/** 行为树资产 JSON 构建辅助（供多个 Capability / Tool 复用）。 */
class NEXUSLINK_API FNexusBehaviorTreeInspectUtils
{
public:
	/**
	 * 递归构建 BT 节点摘要（含路径索引、全局扁平序号、可编辑属性、子节点、槽位装饰器与服务）。
	 * @param Path 从根起的点分 childIndex 路径（根节点传空串，与 manage_asset_behavior_tree 一致）
	 * flatIndex 由内部按先序 DFS 自动分配，根节点为 0。
	 */
	static TSharedPtr<FJsonObject> BuildBTNodeInfo(const UBTNode* Node, const FString& Path = FString());

private:
	/** 带可变计数器的内部递归实现。FlatCounter 按先序 DFS 自增，每个节点分配当前值后自增。 */
	static TSharedPtr<FJsonObject> BuildBTNodeInfoRec(const UBTNode* Node, const FString& Path, int32& FlatCounter);
};
