// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusStringMatchUtils.h"
#include "Internationalization/Regex.h"

bool FNexusStringMatchUtils::Matches(const FString& Text, const FString& Pattern)
{
	if (Pattern.IsEmpty())
	{
		return true;
	}

	// 正则模式：以 "/" 开头和结尾
	if (Pattern.Len() >= 3 && Pattern[0] == TEXT('/') && Pattern[Pattern.Len() - 1] == TEXT('/'))
	{
		const FString RegexPattern = Pattern.Mid(1, Pattern.Len() - 2);
		FRegexPattern CompiledPattern(RegexPattern);
		FRegexMatcher Matcher(CompiledPattern, Text);
		return Matcher.FindNext();
	}

	// 前缀匹配：以 "^" 开头
	if (Pattern.StartsWith(TEXT("^")))
	{
		const FString Prefix = Pattern.Mid(1);
		return Text.StartsWith(Prefix);
	}

	// 后缀匹配：以 "$" 结尾
	if (Pattern.EndsWith(TEXT("$")))
	{
		const FString Suffix = Pattern.Left(Pattern.Len() - 1);
		return Text.EndsWith(Suffix);
	}

	// 默认：子串匹配（不区分大小写）
	return Text.Contains(Pattern);
}

