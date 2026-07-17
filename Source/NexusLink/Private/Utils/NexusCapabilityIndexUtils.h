// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Editor（私有头，仅 NexusMcpToolSearchCapabilities 使用）
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UNexusLinkSettings;
struct FNexusCapabilityDefinition;

/**
 * search_capabilities 工具的评分/目录辅助工具。
 * 封装 Token 评分、Capability 相关度排名、Schema 参数提取、目录构建等无状态算法。
 */
class FNexusCapabilityIndexUtils final
{
public:
	FNexusCapabilityIndexUtils() = delete;

	/**
	 * 从 capability InputSchema 的 properties 提取参数列表。
	 * 返回 [{name, type?, description?, required?}] 数组。
	 */
	static TArray<TSharedPtr<FJsonValue>> ExtractParameters(const TSharedPtr<FJsonObject>& InputSchema);

	/**
	 * 单 token 在 keywords 集合中的最佳匹配分：
	 *   - 完全相等：10；前缀匹配：5；子串匹配（len≥3）：2；无命中：0。
	 */
	static int32 ScoreToken(const FString& Token, const TArray<FString>& Keywords);

	/**
	 * 整个 capability 对 query tokens 的相关度分；返回 0 表示完全不匹配（真 AND 语义）。
	 * 全部 token 命中时额外 +10，鼓励多关键词同时命中的结果排前。
	 */
	static int32 ScoreCapability(const TArray<FString>& Tokens, const TArray<FString>& Keywords);

	/**
	 * 部分匹配评分（OR 语义）：至少 1 个 token 命中即返回 >0。
	 * OutMatchedTokens 返回命中 token 数，用于 relaxed 降级排序。
	 */
	static int32 ScoreCapabilityPartial(const TArray<FString>& Tokens, const TArray<FString>& Keywords,
	                                    int32& OutMatchedTokens);

	/** 将 RelatedCapabilities/Prerequisites/WhenToUse 附加到已有 JsonObject。 */
	static void AttachMetaHints(TSharedPtr<FJsonObject>& Entry, const FNexusCapabilityDefinition& Def);

	/** 构建按 tag 分组的 Capability 目录（query="" 时使用；条目仅 name / 可选 whenToUse）。 */
	static TSharedPtr<FJsonObject> BuildDirectory(const UNexusLinkSettings* Settings);
};
