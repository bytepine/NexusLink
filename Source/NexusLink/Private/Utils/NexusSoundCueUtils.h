// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USoundCue;
class USoundNode;
class USoundWave;
class UClass;

/** SoundCue 节点图编辑辅助（与 get_asset_sound_cue 遍历顺序一致）。 */
class NEXUSLINK_API FNexusSoundCueUtils
{
public:
	FNexusSoundCueUtils() = delete;

	/** 深度优先收集全部节点（与 USoundCue::RecursiveFindAllNodes 顺序一致）。 */
	static void CollectNodesInOrder(USoundCue* Cue, TArray<USoundNode*>& OutNodes);

	/** 按 CollectNodesInOrder 下标取节点。 */
	static USoundNode* FindNodeByIndex(USoundCue* Cue, int32 NodeIndex);

	/** 解析 SoundNode 类名（支持短名与 U 前缀）。 */
	static UClass* ResolveSoundNodeClass(const FString& ClassName, FString& OutError);

	/**
	 * 新增节点。parentNodeIndex&lt;0 且无 FirstNode 时设为根；否则挂到父节点 childSlot。
	 * @param OutNodeIndex 新节点在 CollectNodesInOrder 中的索引（添加后重算）
	 */
	static bool AddNode(USoundCue* Cue, UClass* NodeClass, int32 ParentNodeIndex, int32 ChildSlot,
		USoundWave* OptionalWave, int32& OutNodeIndex, FString& OutError);

	/** 按节点索引删除（含从父节点断开）。 */
	static bool RemoveNode(USoundCue* Cue, int32 NodeIndex, FString& OutError);

	/** 将 childIndex 节点连接到 parentIndex 的 childSlot 槽位。 */
	static bool ConnectNodes(USoundCue* Cue, int32 ParentIndex, int32 ChildSlot, int32 ChildIndex, FString& OutError);
};
