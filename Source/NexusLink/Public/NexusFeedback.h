// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * NexusLink Capability 使用反馈子系统。
 *
 * 记录以下几类痛点并落盘到 .nexus-feedback/feedback.jsonl：
 *   - search_zero      : search_capabilities 无命中
 *   - search_overflow  : search_capabilities 命中过多（超过阈值）
 *   - call_unknown     : call_capability 找不到 capability
 *   - call_disabled    : call_capability 被禁用
 *   - call_arg_invalid : call_capability 参数校验失败（缺少 required 字段或类型不符），与 call_fatal 平级自动埋点
 *   - call_fatal       : call_capability 执行时致命错误（非参数校验问题）
 *   - redundant_call   : 同 capability + identity 的子 section 在短窗口内重复调用（前次已含 sections=["all"]）
 *   - slow_call        : capability 执行耗时超过 SlowCallThresholdMs 阈值
 *   - wrong_tool       : AI 主动上报——选错工具
 *   - misuse           : AI 主动上报——参数/用法错误
 *   - schema_guess     : AI 主动上报——字段含义靠猜
 *   - other            : 其他
 *
 * 所有方法均为静态，无需实例化。
 * 总开关由 UNexusLinkSettings::bEnableFeedback 控制。
 */
struct NEXUSLINK_API FNexusFeedback
{
	/** 反馈条目的输入字段（全部可选，按需填充）。 */
	struct FFields
	{
		FString Tool;           ///< MCP 工具名（如 search_capabilities）
		FString Capability;     ///< Capability 名（call_* 类使用）
		FString Query;          ///< 原始 search query（search_* 类使用）
		int32   MatchCount = 0; ///< 命中数（search_overflow 使用）
		FString ArgsDigest;     ///< 参数 hash 摘要（call_fatal 使用）
		FString ErrorText;      ///< 错误片段前 120 字符（call_* 类使用）
		FString Note;           ///< AI 自由文本描述（manual 必填）
		// submit_feedback 结构化扩展字段
		FString AttemptedArgs;  ///< 触发问题的参数摘要（submit_feedback 可选）
		FString ActualError;    ///< 实际报错片段（submit_feedback 可选）
		FString ExpectedField;  ///< 期望但缺失/猜错的字段名（submit_feedback 可选）
	};

	/**
	 * 自动埋点——由 search_capabilities / call_capability 内部调用。
	 * 30 秒内相同 (category + capability + errorHash) 的重复事件自动节流。
	 */
	static void RecordAuto(const FString& Category, const FFields& Fields);

	/**
	 * 手动反馈——由 submit_feedback MCP 工具调用（AI 主动上报）。
	 * 不做节流，每次调用均落盘。
	 */
	static void RecordManual(const FString& Category, const FFields& Fields);

	/**
	 * 生成 Markdown 聚合报告并归档 JSONL。
	 * 归档路径：.nexus-feedback/archive/feedback_<ts>.jsonl
	 * 报告路径：.nexus-feedback/report_<ts>.md
	 * 完成后清空主 feedback.jsonl。
	 * @return 生成的报告文件绝对路径；失败时返回空字符串。
	 */
	static FString ExportReport();

	/** Issue 预填草稿（标题 + Markdown 正文）。 */
	struct FIssueDraft
	{
		FString Title;
		FString Body;
	};

	/** 从当前 feedback.jsonl 生成 Issue 草稿（不清空、不归档）。 */
	static bool BuildIssueDraft(FIssueDraft& OutDraft);

	/** 构建 GitHub issues/new 预填 URL（title/body 经 UrlEncode）。 */
	static FString BuildIssuePrefillUrl(const FString& Title, const FString& Body);

	/** 读取当前 pending 反馈并在浏览器打开 GitHub Issue 预填页；无数据返回 false。 */
	static bool OpenIssuePrefillInBrowser();

	/**
	 * 构建脱敏参数快照（condensed JSON，敏感 key 值替换为 "<redacted>"，截断 200 字符）。
	 * 供测试与调试使用；生产路径由 call_capability 内部自动调用。
	 */
	static FString BuildRedactedArgsSnapshot(const TSharedPtr<FJsonObject>& Args);

	/** 清空主 feedback.jsonl（不归档）。 */
	static void Clear();

	/** 返回 .nexus-feedback 目录的绝对路径。 */
	static FString GetFeedbackDir();

	/** 返回当前 feedback.jsonl 中的记录行数（0 = 空或不存在）。 */
	static int32 GetRecordCount();
};
