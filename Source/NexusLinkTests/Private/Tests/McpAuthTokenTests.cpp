// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NexusMcpAuth.h"

namespace
{
	// 32 位十六进制才算合法 token
	const FString TokenA = TEXT("0123456789abcdef0123456789abcdef");
	const FString TokenB = TEXT("fedcba9876543210fedcba9876543210");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkMcpAuthTokenParseTest,
	"NexusLink.Auth.ParseAuthTokens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkMcpAuthTokenParseTest::RunTest(const FString& Parameters)
{
	{
		TArray<FString> Out;
		FNexusMcpAuth::ParseAuthTokens(TokenA + TEXT(",") + TokenB, Out);
		TestEqual(TEXT("逗号分隔拆成两个"), Out.Num(), 2);
	}
	{
		TArray<FString> Out;
		FNexusMcpAuth::ParseAuthTokens(TokenA + TEXT(";") + TokenB, Out);
		TestEqual(TEXT("分号分隔拆成两个"), Out.Num(), 2);
	}
	{
		TArray<FString> Out;
		FNexusMcpAuth::ParseAuthTokens(TokenA + TEXT(", \t") + TokenB + TEXT("\n"), Out);
		TestEqual(TEXT("逗号/空白混排拆成两个"), Out.Num(), 2);
	}
	{
		TArray<FString> Out;
		FNexusMcpAuth::ParseAuthTokens(TokenA.ToUpper() + TEXT(",") + TokenA, Out);
		TestEqual(TEXT("大小写视作同一个"), Out.Num(), 1);
		TestEqual(TEXT("统一小写"), Out[0], TokenA);
	}
	{
		TArray<FString> Out;
		FNexusMcpAuth::ParseAuthTokens(TEXT("short,xyz,") + TokenA, Out);
		TestEqual(TEXT("非法项被丢弃"), Out.Num(), 1);
	}

	TestTrue(TEXT("命中 ExtraTokens 中逗号分隔的第二项"),
		FNexusMcpAuth::IsTokenAccepted(TokenB, FString(), TokenA + TEXT(",") + TokenB));
	TestFalse(TEXT("空 Bearer 不通过"),
		FNexusMcpAuth::IsTokenAccepted(FString(), TokenA, FString()));

	return true;
}
