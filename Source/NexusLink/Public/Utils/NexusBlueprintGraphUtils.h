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

	/** 将单个 Pin 序列化为 JSON，包含类型、方向、默认值及连线信息。 */
	static TSharedPtr<FJsonObject> SerializeBPPin(const UEdGraphPin* Pin);

	/** 将单个节点（含所有 Pin）序列化为 JSON。 */
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
};

#endif // WITH_EDITOR
