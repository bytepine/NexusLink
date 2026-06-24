// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 前向声明 Json 类型，避免把 Dom/JsonObject.h / Dom/JsonValue.h 污染到所有 includer
class FJsonObject;
class FJsonValue;

/**
 * Capability 执行结果 —— 替代原 (OutEntries, OutTop, OutCapabilityError) 三出口，
 * 统一为单返回值，避免"两个出口同时写"的语义模糊。
 *
 *   - Entries    : 结果条目列表（等价旧 OutEntries）；Tool 端聚合为 results[]
 *   - TopFields  : 额外写到顶层的字段（等价旧 OutTop；按需构造，多数 cap 无需填）
 *   - FatalError : 非空表示致命错误，Tool 端归入 capabilityErrors[]；Entries 不写
 */
struct FCapabilityResult
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	TSharedPtr<FJsonObject>        TopFields;
	FString                        FatalError;
	/** 标记 FatalError 源于参数校验（required 字段缺失/类型不符），用于区分 call_arg_invalid 与 call_fatal。 */
	bool                           bIsArgInvalid = false;

	/** 工厂方法：快速构造一个只含错误的结果。 */
	static FCapabilityResult MakeFatal(const FString& Error)
	{
		FCapabilityResult R;
		R.FatalError = Error;
		return R;
	}

	/** 工厂方法：参数校验失败（记录为 call_arg_invalid）。 */
	static FCapabilityResult MakeArgInvalid(const FString& Error)
	{
		FCapabilityResult R;
		R.FatalError    = Error;
		R.bIsArgInvalid = true;
		return R;
	}
};

/**
 * Capability 元数据定义 —— Capability 与 Tool 已完全解耦，所有元信息在自身上。
 *
 * 描述格式（四段式，≤100 字符）：
 *   [VERB] [TARGET]. [DIFFERENTIATOR]. [CONSTRAINT?]
 * 详见 CapabilitySpec.md。
 */
struct FNexusCapabilityDefinition
{
	/** Capability 全局唯一标识符（snake_case；search_capabilities / call_capability 直接按此匹配）。 */
	FString Name;

	/**
	 * Capability 描述（四段式，≤100 字符）：
	 *   [VERB TARGET]. [DIFFERENTIATOR]. [CONSTRAINT?]
	 * 注册期自动校验：长度 / name token 重叠率 / 段数（至少含 1 个 '.'）。
	 */
	FString Description;

	/** 输入参数 JSON Schema：`{type:"object", properties:{...}, required?:[...]}`。 */
	TSharedPtr<FJsonObject> InputSchema;

	/**
	 * 标签集合（访问级别 + 功能分类，使用 FNexusMcpTags 常量）。
	 * 设置面板按其中的功能分类（editor/blueprint/material/...）做树状分组；
	 * "只读模式"按 readonly 标签筛选。
	 */
	TArray<FString> Tags;

	/**
	 * 强关联的兄弟 Capability 名称列表。
	 * search_capabilities 命中本 cap 时附带返回，辅助 AI 不必再次搜索。
	 * 典型：get_asset_blueprint ↔ manage_asset_blueprint。
	 */
	TArray<FString> RelatedCapabilities;

	/**
	 * 前置依赖枚举（"pie" / "unlua" / "editor_only" / "ds_mode"）。
	 * search_capabilities 返回时作为 prerequisites 字段透传，提醒 AI 检查运行环境。
	 */
	TArray<FString> Prerequisites;

	/**
	 * 同类多选时的"该选我"短语（≤40 字符，可选）。
	 * 仅在同前缀 cap ≥3 且 description 难以区分时填写；其余 cap 留空。
	 * search_capabilities 命中 3-8 条时随结果返回。
	 */
	FString WhenToUse;

	/**
	 * 子类特有搜索关键词（同义词/领域词）。
	 * 注册期与 Name/Description 分词合并去重后存入 FCapRecord::Keywords。
	 * name token 重叠词注册期自动剥离。
	 */
	TArray<FString> ExtraSearchKeywords;

	bool HasTag(const FString& Tag) const { return Tags.Contains(Tag); }
};

/**
 * MCP Capability —— 一段可执行的工作单元，通过 REGISTER_MCP_CAPABILITY 自注册到全局表，
 * 由 search_capabilities / call_capability 元工具索引和驱动。
 *
 * 子类必须实现的两个纯虚钩子：
 *   - BuildDefinition : 一次性填写所有元数据（Name/Description/InputSchema/Tags/...）
 *   - Execute         : 完整执行逻辑；自行提取参数、循环、写 OutEntries / OutTop
 *
 * 致命错误协议：
 *   - Execute 通过 FCapabilityResult.FatalError 上报致命错误
 *   - Tool 端把该错误归入 capabilityErrors[] 字段，不影响其他 Capability 继续执行
 *
 * 单条失败协议：
 *   - Execute 向 Entries 写入含 "error" 字段的 entry 视为 fail；否则 success
 */
class NEXUSLINK_API FNexusCapability
{
public:
	virtual ~FNexusCapability() = default;

	/**
	 * 返回元数据定义（首次调用触发 BuildDefinition，后续使用实例级缓存）。
	 * 非虚，子类不可重写。
	 */
	const FNexusCapabilityDefinition& GetDefinition() const;

	/**
	 * 主入口：包含通用守卫（Args 兜底、required 字段校验、执行计时日志），
	 * 通过后调用子类 Execute() 并返回 FCapabilityResult。
	 * call_capability 直接使用返回值，不再通过出参传递。
	 */
	FCapabilityResult Run(const TSharedPtr<FJsonObject>& Arguments) const;

	// ── 子类样板 helper（protected static，opt-in，新 cap 推荐使用）──────────

	/**
	 * 守卫：若 Args 中缺少 Key 字段，向 OutEntries 写入带 error 的条目并返回 false，
	 * 调用方直接 return 即可。Locator 为 entry 中的定位字段（如 {"assetPath", "/Game/X"}）。
	 */
	static bool RequireString(const TSharedPtr<FJsonObject>& Args,
	                          const TCHAR* Key,
	                          FString& OutValue,
	                          TArray<TSharedPtr<FJsonValue>>& OutEntries,
	                          const TMap<FString, FString>& Locator = {});

	/**
	 * 向 OutEntries 写入一条带 Locator 定位字段的详情条目。
	 * Detail 中的所有字段会合并到同一个 JsonObject（与 Locator 并列，非嵌套）。
	 */
	static void EmitEntry(TArray<TSharedPtr<FJsonValue>>& OutEntries,
	                      const TMap<FString, FString>& Locator,
	                      const TSharedPtr<FJsonObject>& Detail);

	/** 向 OutEntries 写入一条带 Locator 定位字段的失败条目（含 "error" 字段）。 */
	static void EmitError(TArray<TSharedPtr<FJsonValue>>& OutEntries,
	                      const TMap<FString, FString>& Locator,
	                      const FString& Error);

protected:
	/**
	 * 填写本 Capability 的所有元数据：
	 *   Out.Name / Out.Description / Out.InputSchema / Out.Tags
	 *   Out.ExtraSearchKeywords / Out.RelatedCapabilities / Out.Prerequisites / Out.WhenToUse
	 * 仅在首次 GetDefinition() 时调用一次，结果被实例级缓存。
	 */
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const = 0;

	/**
	 * 完整执行逻辑：子类自主处理参数提取、循环，返回 FCapabilityResult。
	 *
	 * 失败协议：
	 *   - 致命错误（无法处理任何 item）→ 填 Result.FatalError，Entries 留空
	 *   - 单条失败 → Result.Entries 中写含 "error" 字段的 entry，FatalError 留空
	 */
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const = 0;

private:
	mutable FNexusCapabilityDefinition CachedDef;
	mutable bool                       bDefBuilt = false;
};
