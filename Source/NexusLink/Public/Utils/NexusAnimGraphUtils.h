// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Domain — §7.4 豁免：单 Capability 引用但聚合 6 个 helper 函数
#include "CoreMinimal.h"

class UAnimBlueprint;
class UEdGraph;
class UAnimGraphNode_StateMachineBase;
class UAnimationStateMachineGraph;
class UAnimStateNodeBase;
class UAnimStateNode;
class UAnimStateTransitionNode;

/** AnimBlueprint 状态机 EdGraph 查找辅助。 */
class NEXUSLINK_API FNexusAnimGraphUtils
{
public:
	/**
	 * 在 AnimBP 的 FunctionGraphs 中按名称定位 AnimGraph。
	 * GraphName 为空时返回首个名为 "AnimGraph" 的图。
	 */
	static UEdGraph* FindAnimGraph(UAnimBlueprint* BP, const FString& GraphName);

	/** 在 AnimGraph 中按节点名定位状态机节点。 */
	static UAnimGraphNode_StateMachineBase* FindStateMachineNode(UEdGraph* AnimGraph, const FString& NodeName);

	/** 取状态机节点的 EditorStateMachineGraph（实际类型 UAnimationStateMachineGraph）。 */
	static UAnimationStateMachineGraph* GetStateMachineGraph(UAnimGraphNode_StateMachineBase* SMNode);

	/** 在状态机子图中按状态名定位 UAnimStateNode。匹配 NodeName 或 BoundGraph 名。 */
	static UAnimStateNode* FindStateByName(UAnimationStateMachineGraph* SMGraph, const FString& StateName);

	/**
	 * 在状态机子图中查找连接 source → target 的 transition 节点。
	 * 返回 nullptr 表示不存在。Source/Target 用 state 节点名。
	 */
	static UAnimStateTransitionNode* FindTransition(UAnimationStateMachineGraph* SMGraph,
	                                                const FString& SourceStateName,
	                                                const FString& TargetStateName);

	/**
	 * 取 UAnimStateNodeBase 的输出 Pin（自身的 outgoing Pin）。
	 * 优先调用 GetOutputPin()；不可用时回退到 Pins[] 按 Direction 筛选。
	 */
	static class UEdGraphPin* GetStateOutputPin(UAnimStateNodeBase* StateNode);

	/** 取 UAnimStateNodeBase 的输入 Pin（incoming Pin）。 */
	static class UEdGraphPin* GetStateInputPin(UAnimStateNodeBase* StateNode);

	/**
	 * 解析 AnimGraph 节点类（可带 AnimGraphNode_ / UAnimGraphNode_ 前缀）。
	 * 播放：SequencePlayer / SequenceEvaluator / BlendSpacePlayer(=BlendSpace1D) / BlendSpaceEvaluator / RandomPlayer / PoseBlendNode / PoseByName
	 * 混合：Slot / Blend(=BlendListByBool) / BlendListByEnum / BlendListByInt / MultiWayBlend / LayeredBoneBlend / ApplyAdditive / SaveCachedPose / UseCachedPose / Inertialization
	 * 空间：ComponentToLocalSpace / LocalToComponentSpace
	 * 骨骼：TwoBoneIK / FABRIK / CCDIK / LookAt / ModifyBone / CopyBone / HandIKRetargeting / AimOffset / AimOffsetLookAt
	 * 可选：ControlRig（需 WITH_CONTROL_RIG）
	 */
	static UClass* ResolveAnimGraphNodeClass(const FString& NodeClass);

	/** 在图中按 GUID 或标题查找节点。 */
	static class UEdGraphNode* FindNodeByGuidOrTitle(UEdGraph* Graph, const FString& GuidOrTitle);

	/** 在 AnimGraph 中生成节点并 AllocateDefaultPins。 */
	static class UEdGraphNode* SpawnAnimGraphNode(UEdGraph* Graph, UClass* NodeClass, int32 PosX, int32 PosY, FString& OutError);

	/** 写 IK / LookAt / ModifyBone 的骨骼名（boneName）。 */
	static void ApplyBoneName(class UEdGraphNode* Node, const FString& BoneName);

	/** 连接两节点引脚（按引脚名，忽略方向前缀差异）。 */
	static bool ConnectAnimPins(class UEdGraphNode* Source, const FString& SourcePin,
		class UEdGraphNode* Target, const FString& TargetPin, FString& OutError);
};

