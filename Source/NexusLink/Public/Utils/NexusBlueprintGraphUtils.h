// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Asset（仅 WITH_EDITOR）
#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UFunction;

/**
 * Blueprint 蓝图图/节点/Pin 结构操作与序列化公共工具。
 * 供 get_asset_blueprint 与 manage_asset_blueprint 共用，消除两侧重复的静态辅助函数。
 */
class NEXUSLINK_API FNexusBlueprintGraphUtils final
{
public:
	FNexusBlueprintGraphUtils() = delete;

	/** 收集 BP 内所有层级子图（含嵌套 StateMachine/State/Transition 图），唯一性保证。 */
	static void CollectAllGraphs(UBlueprint* BP, TArray<UEdGraph*>& OutGraphs);

	/** 按名称查找 BP 内的图（使用 CollectAllGraphs 后精确匹配），未找到返回 nullptr。 */
	static UEdGraph* FindBPGraph(UBlueprint* BP, const FString& GraphName);

	/** 按 GUID 字符串查找图内节点，解析失败或不存在均返回 nullptr。 */
	static UEdGraphNode* FindBPNode(UEdGraph* Graph, const FString& NodeIdStr);

	/**
	 * 按 Pin 名称查找节点上的 Pin：PinName 精确 → PinName 忽略大小写 → PinFriendlyName 忽略大小写。
	 * 未找到返回 nullptr。
	 */
	static UEdGraphPin* FindBPPin(UEdGraphNode* Node, const FString& PinName);

	/** 节点可用 PinName 列表（用于引脚未找到时的错误提示）。 */
	static FString FormatBPPinNameHint(UEdGraphNode* Node, int32 MaxNames = 12);

	/** Pin 方向枚举 → 小写字符串（"input" / "output" / "unknown"）。 */
	static FString PinDirectionToString(EEdGraphPinDirection Dir);

	/** 将单个 Pin 序列化为 JSON：direction / pinCategory / containerType / isReference / isConst / bOrphan 始终写出。 */
	static TSharedPtr<FJsonObject> SerializeBPPin(const UEdGraphPin* Pin);

	/** 将单个节点（含所有 Pin）序列化为 JSON；bIsNodeEnabled 始终写出。 */
	static TSharedPtr<FJsonObject> SerializeBPNode(const UEdGraphNode* Node);

	/**
	 * 按图的 UClass 名推断语义类型字符串。
	 * 返回值：event / function / macro / animgraph / statemachine / state / transition / conduit / unknown
	 */
	static FString GetBPGraphType(const UEdGraph* Graph);

	/** 取图的直接父图名称（适用于嵌套子图）；无父图时返回空字符串。 */
	static FString GetBPParentGraphName(const UEdGraph* Graph);

	/** 构建图摘要 JSON（name / graphType / enabledNodeCount / disabledNodeCount / parentGraph）。 */
	static TSharedPtr<FJsonObject> BuildBPGraphSummary(const UEdGraph* Graph);

	/** 聚合 bOrphanedPin，上限 MaxCount 条：nodeId / pinName。 */
	static void CollectOrphanedPins(UBlueprint* BP, TArray<TSharedPtr<FJsonObject>>& OutPins, int32 MaxCount = 32);

	/**
	 * 从 Event 节点沿 exec pin 走线性链。上限 MaxPaths 条 × MaxNodes 节点。
	 * 每条：start / nodes[{nodeId,nodeTitle}]。
	 */
	static void CollectExecPaths(UEdGraph* Graph, TArray<TSharedPtr<FJsonObject>>& OutPaths,
		int32 MaxPaths = 16, int32 MaxNodes = 32);

	// ── 写侧：add_node 各分支共用的节点创建 + 放置（消除 manage cap 里 5 处重复的放置样板） ──

	/** 把新建节点挂进图并完成 Guid/Pin 初始化与定位（节点创建各分支共用尾巴）。 */
	static void PlaceNewNode(UEdGraph* Graph, UEdGraphNode* Node, int32 PosX, int32 PosY);

	/** 三级回退解析函数：指定类 → 全局同名 → 全局 "K2_" 前缀；均未命中返回 nullptr。 */
	static UFunction* ResolveGraphFunction(const FString& FuncName, const FString& FuncClassName);

	/** 新建 K2Node_CallFunction 并放置。 */
	static UEdGraphNode* MakeCallFunctionNode(UEdGraph* Graph, UFunction* Func, int32 PosX, int32 PosY);

	/** 取已有 Event override 或新建 K2Node_Event 并放置。 */
	static UEdGraphNode* MakeEventNode(UBlueprint* BP, UEdGraph* Graph, const FString& EventName, UClass* EventClass, int32 PosX, int32 PosY);

	/** 新建 self 成员变量的 Get/Set 节点并放置（bSetter 选 VariableSet）。 */
	static UEdGraphNode* MakeVariableNode(UEdGraph* Graph, const FString& VarName, bool bSetter, int32 PosX, int32 PosY);

	/** 按 UClass 新建任意 UEdGraphNode 子类并放置。 */
	static UEdGraphNode* MakeGenericNode(UEdGraph* Graph, UClass* NodeClass, int32 PosX, int32 PosY);
};

#endif // WITH_EDITOR
