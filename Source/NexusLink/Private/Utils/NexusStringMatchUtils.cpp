// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusStringMatchUtils.h"
#include "Internationalization/Regex.h"

FNexusCompiledStringPattern::FNexusCompiledStringPattern(const FString& Pattern)
{
	if (Pattern.IsEmpty())
	{
		Kind = EKind::All;
		return;
	}

	if (Pattern.Len() >= 3 && Pattern[0] == TEXT('/') && Pattern[Pattern.Len() - 1] == TEXT('/'))
	{
		Kind = EKind::Regex;
		Regex = MakeShareable(new FRegexPattern(Pattern.Mid(1, Pattern.Len() - 2)));
		return;
	}

	if (Pattern.StartsWith(TEXT("^")))
	{
		Kind = EKind::Prefix;
		Needle = Pattern.Mid(1);
		return;
	}

	if (Pattern.EndsWith(TEXT("$")))
	{
		Kind = EKind::Suffix;
		Needle = Pattern.Left(Pattern.Len() - 1);
		return;
	}

	Kind = EKind::Substring;
	Needle = Pattern;
}

FNexusCompiledStringPattern::~FNexusCompiledStringPattern() = default;

bool FNexusCompiledStringPattern::Matches(const FString& Text) const
{
	switch (Kind)
	{
	case EKind::All:
		return true;
	case EKind::Regex:
		if (!Regex.IsValid())
		{
			return false;
		}
		{
			FRegexMatcher Matcher(*Regex, Text);
			return Matcher.FindNext();
		}
	case EKind::Prefix:
		return Text.StartsWith(Needle);
	case EKind::Suffix:
		return Text.EndsWith(Needle);
	case EKind::Substring:
	default:
		return Text.Contains(Needle, ESearchCase::IgnoreCase);
	}
}

bool FNexusStringMatchUtils::Matches(const FString& Text, const FString& Pattern)
{
	return FNexusCompiledStringPattern(Pattern).Matches(Text);
}
