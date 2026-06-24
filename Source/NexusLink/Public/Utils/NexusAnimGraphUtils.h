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
};
