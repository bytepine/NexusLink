// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRegexPattern;

/**
 * 预编译的字符串匹配 Pattern，供同一 Pattern 对多条文本反复匹配
 *（避免每次重建 FRegexPattern）。语义与 FNexusStringMatchUtils::Matches 一致。
 */
class NEXUSLINK_API FNexusCompiledStringPattern
{
public:
	explicit FNexusCompiledStringPattern(const FString& Pattern);
	FNexusCompiledStringPattern(const FNexusCompiledStringPattern&) = default;
	FNexusCompiledStringPattern(FNexusCompiledStringPattern&&) = default;
	FNexusCompiledStringPattern& operator=(const FNexusCompiledStringPattern&) = default;
	FNexusCompiledStringPattern& operator=(FNexusCompiledStringPattern&&) = default;
	~FNexusCompiledStringPattern();

	bool Matches(const FString& Text) const;

private:
	enum class EKind : uint8 { All, Regex, Prefix, Suffix, Substring };
	EKind Kind = EKind::All;
	FString Needle;
	TSharedPtr<FRegexPattern> Regex;
};

/**
 * 统一字符串匹配工具。
 * 供所有 MCP Tool 的过滤参数共用（nameFilter/textFilter/classFilter 等）。
 *
 * 匹配规则：
 * - 以 "/" 开头和结尾视为正则表达式，如 "/^BP_.+Actor$/"
 * - 以 "^" 开头视为前缀匹配
 * - 以 "$" 结尾视为后缀匹配
 * - 其他情况为子串匹配（不区分大小写）
 */
// Utils 层：Common
class NEXUSLINK_API FNexusStringMatchUtils
{
public:
	/**
	 * 判断 Text 是否匹配 Pattern。
	 * Pattern 为空时始终返回 true（无过滤）。
	 */
	static bool Matches(const FString& Text, const FString& Pattern);
};

