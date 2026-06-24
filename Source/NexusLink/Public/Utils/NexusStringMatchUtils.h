// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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

