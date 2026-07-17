// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * JSON 响应压缩器：把数组条目里重复出现的"主流值字段"抽到顶部 defaults，
 * 条目里等于该默认值的同名字段随即省略，AI 合并视图时"缺省即默认"。
 *
 * 设计要点：
 * - 仅抽取标量字段（string / number / bool / null），不处理 object/array
 * - 强制默认（ForcedDefault）：由入参决定、一定不会变，立即生效
 * - 候选字段（Candidate）：运行时统计主流值分布，满足数量/占比/净收益三阈值才抽取
 * - 抽取后的条目**保留**落在非默认分支的同名字段，消费方合并时逐条覆写 defaults
 *
 * 两种使用姿势：
 *
 * 1) **自动模式（推荐，默认路径）**：不需要工具侧任何声明。
 *    `NexusMcpDispatcher` 在每次工具执行后自动对 `StructuredContent` 调用
 *    `AutoCompactRecursive`，递归遍历所有嵌套对象 / 数组并对"对象数组"字段启用
 *    `SetAutoDiscover(true)` 尝试抽取；满足三阈值则在同级写入 `<fieldName>_defaults`，
 *    否则条目保持原样。三阈值（`MinCount=2` / `MinMatchRatio=0.7` / `MinNetSaveBytes=20`）
 *    兜底避免小响应上产生负收益。
 *
 * 2) **手动模式（定制化场景）**：需要强制默认（`ForcedDefault`，如 `get_output_log`
 *    的 `categoryFilter` 回显）、或需要在统计阈值之外强制抽取特定字段时使用。
 *    典型用法：
 *      FNexusResponseCompactor C;
 *      C.AddForcedDefault(TEXT("assetType"), TEXT("Blueprint"));
 *      C.AddCandidate(TEXT("kind"));
 *      C.CompactArray(PageArray);
 *      C.Emit(ResultObj, TEXT("properties"));   // 写入 properties_defaults
 *    手动写入 `<prefix>_defaults` 后，自动模式检测到同级已有此字段会跳过，避免双写。
 *
 * 消费侧合并规则：merged = {**defaults, **entry}（entry 覆盖 defaults 同名字段）
 */
// Utils 层：Common
class NEXUSLINK_API FNexusResponseCompactorUtils
{
public:
	/** 候选字段至少需要 N 条数据才做统计抽取。 */
	int32 MinCount = 2;
	/** 主流值占比下限（相对于持有该字段的条目数）。 */
	float MinMatchRatio = 0.7f;
	/** 净收益字节下限，低于此值认为压缩不划算。 */
	int32 MinNetSaveBytes = 20;

	/** 声明一个需要运行时统计的候选字段。 */
	void AddCandidate(const FString& FieldName);

	/** 入参驱动的强制默认（例如 assetType 回显）。 */
	void AddForcedDefault(const FString& FieldName, const TSharedPtr<FJsonValue>& Value);
	void AddForcedDefault(const FString& FieldName, const FString& StringValue);
	void AddForcedDefault(const FString& FieldName, bool bBoolValue);

	/**
	 * 开启自动扫描模式：CompactArray 除处理显式 Candidates 外，还会自动扫描条目里
	 * 所有其他标量字段并尝试统计抽取，同样受三阈值约束。
	 * 内置排除集已含 name/path/assetPath/nodeId/tag/message/timestamp/frame/id/label/text/error；
	 * AdditionalExclusions 可追加业务特有的身份字段。
	 */
	void SetAutoDiscover(bool bEnable, TArray<FString> AdditionalExclusions = {});

	/** 执行压缩（就地修改数组里的 FJsonObject 条目）。 */
	void CompactArray(TArray<TSharedPtr<FJsonValue>>& Items);

	/** 是否实际产生了默认值（用于条件输出）。 */
	bool HasDefaults() const { return Defaults.IsValid() && Defaults->Values.Num() > 0; }

	/**
	 * 将 defaults 写入 Parent：<Prefix>_defaults : {字段名 -> 主流值}
	 * 仅当 HasDefaults() 为 true 时写入；Prefix 例如 "properties" / "assets" / "actors"。
	 */
	void Emit(const TSharedPtr<FJsonObject>& Parent, const FString& Prefix) const;

	const TSharedPtr<FJsonObject>& GetDefaults() const { return Defaults; }

	/**
	 * 自动压缩整棵响应树：对 Parent 内每个"对象数组"字段 K
	 * （K 不是 `_defaults` / `defaults` / `content` 等已知语义冲突名、
	 *   且同级尚未出现 `<K>_defaults`）启用自动扫描尝试抽取，
	 * 命中阈值则写入 `<K>_defaults`；然后递归进入：
	 *   - 每个 object 字段
	 *   - 每个 array 元素里的 object
	 * 便于 `NexusMcpDispatcher` 统一为所有工具打上"默认压缩能力"，
	 * 工具侧无需再手动声明候选字段。
	 *
	 * 受 `UNexusLinkSettings::bCompactResponseDefaults` 总开关控制：
	 * 关闭时整个 Pass 直接跳过。
	 */
	static void AutoCompactRecursive(const TSharedPtr<FJsonObject>& Parent);

private:
	/** 候选字段声明顺序（参与统计）。 */
	TArray<FString> Candidates;
	/** 入参驱动的强制默认。 */
	TMap<FString, TSharedPtr<FJsonValue>> ForcedDefaults;
	/** 压缩完成后实际写入的 defaults 对象。 */
	TSharedPtr<FJsonObject> Defaults;

	/** 自动扫描所有标量字段；false 时只处理显式 Candidates。 */
	bool bAutoDiscover = false;
	/** 调用方追加的业务特有排除字段；内置 13 个身份字段走 cpp 侧进程级共享 TSet，避免 per-instance 重复分配。 */
	TSet<FString> ExtraAutoDiscoverExclusions;

	/**
	 * 对单个字段执行分布统计 + 阈值判定 + 抽取。
	 * 被 CompactArray 的 Candidates 循环和自动扫描路径共同调用。
	 */
	void TryCompactField(const FString& Field, TArray<TSharedPtr<FJsonValue>>& Items);
};

