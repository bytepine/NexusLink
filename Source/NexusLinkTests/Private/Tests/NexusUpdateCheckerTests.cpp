// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NexusUpdateChecker.h"

// ═══════════════════════════════════════════════════════════════════════════
// FNexusUpdateChecker 单元测试：IsNewerVersion / HTML 解析 / GetCurrentVersion。
// 纯 C++，无需 HTTP 或网络。
// ═══════════════════════════════════════════════════════════════════════════

// ── 1. IsNewerVersion ─────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkUpdateCheckerIsNewerVersionTest,
	"NexusLink.UpdateChecker.IsNewerVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkUpdateCheckerIsNewerVersionTest::RunTest(const FString& /*Parameters*/)
{
	TestTrue(TEXT("1.14.0 > 1.13.1"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("1.14.0"), TEXT("1.13.1")));
	TestFalse(TEXT("1.13.1 > 1.14.0"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("1.13.1"), TEXT("1.14.0")));
	TestFalse(TEXT("equal versions not newer"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("1.13.1"), TEXT("1.13.1")));
	TestTrue(TEXT("2.0.0 > 1.99.99"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("2.0.0"), TEXT("1.99.99")));
	TestTrue(TEXT("1.10.0 > 1.9.0"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("1.10.0"), TEXT("1.9.0")));
	TestFalse(TEXT("1.0 == 1.0.0 (padded segments)"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("1.0"), TEXT("1.0.0")));
	TestTrue(TEXT("1.0.1 > 1.0"),
		FNexusUpdateChecker::IsNewerVersion(TEXT("1.0.1"), TEXT("1.0")));
	return true;
}

// ── 2. ParseLatestTagFromReleasePage ──────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkUpdateCheckerParseReleasePageTest,
	"NexusLink.UpdateChecker.ParseReleasePage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkUpdateCheckerParseReleasePageTest::RunTest(const FString& /*Parameters*/)
{
	// og:url 优先
	{
		const FString Html = TEXT(
			R"(<meta property="og:url" content="https://github.com/bytepine/NexusLink/releases/tag/v1.14.0" />)");
		TestEqual(TEXT("og:url → 1.14.0"),
			FNexusUpdateChecker::ParseLatestTagFromReleasePage(Html), TEXT("1.14.0"));
	}

	// canonical 兜底
	{
		const FString Html = TEXT(
			R"(<link rel="canonical" href="https://github.com/bytepine/NexusLink/releases/tag/v2.0.0">)");
		TestEqual(TEXT("canonical → 2.0.0"),
			FNexusUpdateChecker::ParseLatestTagFromReleasePage(Html), TEXT("2.0.0"));
	}

	// 全文首个 /releases/tag/ 兜底（无前导 v）
	{
		const FString Html = TEXT(
			"Visit https://github.com/bytepine/NexusLink/releases/tag/1.13.1 for details.");
		TestEqual(TEXT("fallback path → 1.13.1"),
			FNexusUpdateChecker::ParseLatestTagFromReleasePage(Html), TEXT("1.13.1"));
	}

	// og:url 存在时忽略后续 tag
	{
		const FString Html = TEXT(
			"<meta property=\"og:url\" content=\"https://github.com/bytepine/NexusLink/releases/tag/v3.1.0\">"
			"https://github.com/bytepine/NexusLink/releases/tag/9.9.9");
		TestEqual(TEXT("og:url wins over later tag"),
			FNexusUpdateChecker::ParseLatestTagFromReleasePage(Html), TEXT("3.1.0"));
	}

	// 无 tag → 空
	{
		TestTrue(TEXT("no tag → empty"),
			FNexusUpdateChecker::ParseLatestTagFromReleasePage(TEXT("<html></html>")).IsEmpty());
	}

	return true;
}

// ── 3. GetCurrentVersion 冒烟 ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkUpdateCheckerGetCurrentVersionTest,
	"NexusLink.UpdateChecker.GetCurrentVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkUpdateCheckerGetCurrentVersionTest::RunTest(const FString& /*Parameters*/)
{
	const FString Version = FNexusUpdateChecker::GetCurrentVersion();
	TestFalse(TEXT("GetCurrentVersion not unknown"), Version.Equals(TEXT("unknown")));
	TestFalse(TEXT("GetCurrentVersion not empty"), Version.IsEmpty());
	// VersionName 格式为 X.Y.Z（源码树可为 0.0.0；发版 zip 由 build_unreal.py 注入真实版本）
	TestTrue(TEXT("Version contains dot separator"), Version.Contains(TEXT(".")));
	return true;
}
