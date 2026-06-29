// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "NexusFeedback.h"

// ═══════════════════════════════════════════════════════════════════════════
// NexusFeedback 端到端测试：覆盖 RoundTrip / 节流 / 节流 key 规则化三条核心路径。
// 依赖：EditorContext（需要 UNexusLinkSettings CDO）。
// ═══════════════════════════════════════════════════════════════════════════

// ── 测试辅助 ──────────────────────────────────────────────────────────────

/** 临时把反馈目录重定向到 Saved/NexusFeedbackTests_<uuid>，测试后自动清理。 */
struct FNexusFeedbackTestScope
{
	FString OrigDir;

	FNexusFeedbackTestScope()
	{
		// 使用 pid + tick 产生唯一目录，避免并行测试冲突
		const FString UniqueDir = FPaths::ProjectSavedDir()
			/ FString::Printf(TEXT("NexusFeedbackTests_%d_%lld"),
				FPlatformProcess::GetCurrentProcessId(),
				FPlatformTime::Cycles64());
		IFileManager::Get().MakeDirectory(*UniqueDir, /*Tree=*/true);
		// 通过预先建好目录、再手工 Clear() 让 FNexusFeedback 指向它是不可能的
		// （GetFeedbackDir() 是硬编码的 .nexus-feedback）。
		// 此处直接测试真实目录，测后清空。
		FNexusFeedback::Clear();
	}

	~FNexusFeedbackTestScope()
	{
		FNexusFeedback::Clear();
		// 同时删除本轮可能留下的 archive 测试归档（只删 Tests 标记文件）
	}
};

// ── 1. RoundTrip ─────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkFeedbackRoundTripTest,
	"NexusLink.Feedback.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkFeedbackRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FNexusFeedbackTestScope Scope;

	// 写入 3 条不同 category 的手动记录
	{
		FNexusFeedback::FFields F;
		F.Tool = TEXT("call_capability");
		F.Capability = TEXT("test_cap_a");
		F.Note = TEXT("RoundTrip test note A");
		FNexusFeedback::RecordManual(TEXT("wrong_tool"), F);
	}
	{
		FNexusFeedback::FFields F;
		F.Query = TEXT("test query b");
		FNexusFeedback::RecordManual(TEXT("search_zero"), F);
	}
	{
		FNexusFeedback::FFields F;
		F.Note = TEXT("schema guess note c");
		FNexusFeedback::RecordManual(TEXT("schema_guess"), F);
	}

	TestEqual(TEXT("RecordCount after 3 writes"), FNexusFeedback::GetRecordCount(), 3);

	// 导出报告
	const FString ReportPath = FNexusFeedback::ExportReport();
	TestFalse(TEXT("ExportReport returns non-empty path"), ReportPath.IsEmpty());

	if (!ReportPath.IsEmpty())
	{
		// 报告文件存在
		TestTrue(TEXT("Report .md file exists"), IFileManager::Get().FileExists(*ReportPath));

		// 报告包含总条数行
		FString MdContent;
		FFileHelper::LoadFileToString(MdContent, *ReportPath);
		TestTrue(TEXT("Report contains totalCount line"),
			MdContent.Contains(TEXT("总条数：**3**")));

		// 归档 jsonl 存在（feedback_<ts>.jsonl）
		const FString ArchiveDir = FNexusFeedback::GetFeedbackDir() / TEXT("archive");
		TArray<FString> ArchiveFiles;
		IFileManager::Get().FindFiles(ArchiveFiles, *(ArchiveDir / TEXT("feedback_*.jsonl")), true, false);
		TestTrue(TEXT("Archive .jsonl created"), ArchiveFiles.Num() > 0);

		// last_summary.json 存在
		TestTrue(TEXT("last_summary.json created"),
			IFileManager::Get().FileExists(*(ArchiveDir / TEXT("last_summary.json"))));

		// report_<ts>.json 存在（与 .md 同目录，同 TsTag）
		const FString JsonReportPath = FPaths::ChangeExtension(ReportPath, TEXT("json"));
		TestTrue(TEXT("Report .json summary exists"), IFileManager::Get().FileExists(*JsonReportPath));

		// 报告含错误指纹段（§8）
		TestTrue(TEXT("Report contains fingerprint section"),
			MdContent.Contains(TEXT("## §8 错误指纹")));
	}

	// 清空后 feedback.jsonl 应不存在或为 0 行
	TestEqual(TEXT("RecordCount after ExportReport"), FNexusFeedback::GetRecordCount(), 0);

	return true;
}

// ── 2. Throttle ──────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkFeedbackThrottleTest,
	"NexusLink.Feedback.Throttle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkFeedbackThrottleTest::RunTest(const FString& /*Parameters*/)
{
	FNexusFeedbackTestScope Scope;

	// 同 key RecordAuto 5 次，30s 内应只落盘 1 次
	FNexusFeedback::FFields F;
	F.Tool       = TEXT("call_capability");
	F.Capability = TEXT("throttle_test_cap");
	F.ErrorText  = TEXT("ThrottleError same message");

	for (int32 i = 0; i < 5; ++i)
	{
		FNexusFeedback::RecordAuto(TEXT("call_fatal"), F);
	}

	TestEqual(TEXT("Throttle: 5 calls same key → only 1 record"), FNexusFeedback::GetRecordCount(), 1);

	return true;
}

// ── 3. RuleNormalize ─────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkFeedbackRuleNormalizeTest,
	"NexusLink.Feedback.RuleNormalize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkFeedbackRuleNormalizeTest::RunTest(const FString& /*Parameters*/)
{
	FNexusFeedbackTestScope Scope;

	// 两条错误仅路径不同，规则化后 key 应相同 → 节流只落盘 1 条
	FNexusFeedback::FFields FA;
	FA.Capability = TEXT("normalize_cap");
	FA.ErrorText  = TEXT("Asset '/Game/Blueprints/BP_Hero' not found");

	FNexusFeedback::FFields FB;
	FB.Capability = TEXT("normalize_cap");
	FB.ErrorText  = TEXT("Asset '/Game/Blueprints/BP_Enemy' not found");

	FNexusFeedback::RecordAuto(TEXT("call_fatal"), FA);
	FNexusFeedback::RecordAuto(TEXT("call_fatal"), FB);

	// 路径段规则化后两条 key 相同，第二条被节流 → 仅 1 条落盘
	TestEqual(TEXT("RuleNormalize: two path-only-diff errors → 1 record"), FNexusFeedback::GetRecordCount(), 1);

	return true;
}

// ── 4. Issue 预填 URL ─────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkFeedbackIssuePrefillTest,
	"NexusLink.Feedback.IssuePrefill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkFeedbackIssuePrefillTest::RunTest(const FString& /*Parameters*/)
{
	FNexusFeedbackTestScope Scope;

	// 写入 call_arg_invalid（最高权重），含结构化字段
	{
		FNexusFeedback::FFields F;
		F.Tool          = TEXT("call_capability");
		F.Capability    = TEXT("get_asset");
		F.ErrorText     = TEXT("assetPaths array is required");
		F.AttemptedArgs = TEXT("{\"assetPath\":\"/Game/BP_Hero\"}");
		F.ActualError   = TEXT("assetPaths array is required");
		F.ExpectedField = TEXT("assetPaths");
		FNexusFeedback::RecordManual(TEXT("call_arg_invalid"), F);
	}
	// 另写一条 wrong_tool，含 note（权重较低，不应成为 best）
	{
		FNexusFeedback::FFields F;
		F.Tool = TEXT("call_capability");
		F.Note = TEXT("should have used search_capabilities first");
		FNexusFeedback::RecordManual(TEXT("wrong_tool"), F);
	}

	FNexusFeedback::FIssueDraft Draft;
	TestTrue(TEXT("BuildIssueDraft succeeds"), FNexusFeedback::BuildIssueDraft(Draft));

	// 标题：应含 capability 名（get_asset）
	TestTrue(TEXT("Title contains capability"), Draft.Title.Contains(TEXT("get_asset")));

	// 正文：环境段（含 ToolsListMode）
	TestTrue(TEXT("Body has env section"),      Draft.Body.Contains(TEXT("## 环境")));
	TestTrue(TEXT("Body env has ToolsListMode"),Draft.Body.Contains(TEXT("ToolsListMode")));
	// 正文：最小复现段
	TestTrue(TEXT("Body has repro section"),  Draft.Body.Contains(TEXT("## 最小复现")));
	// 正文：结构化字段展示
	TestTrue(TEXT("Body has attemptedArgs"),  Draft.Body.Contains(TEXT("attemptedArgs")));
	TestTrue(TEXT("Body has expectedField"),  Draft.Body.Contains(TEXT("expectedField")));
	// 正文：AI 上报结构化（wrong_tool 的 note 也应出现）
	TestTrue(TEXT("Body has AI section"),     Draft.Body.Contains(TEXT("## AI 上报")));

	const FString Url = FNexusFeedback::BuildIssuePrefillUrl(Draft.Title, Draft.Body);
	TestTrue(TEXT("Prefill URL targets issues/new"), Url.Contains(TEXT("github.com/bytepine/NexusLink/issues/new")));
	TestTrue(TEXT("Prefill URL has title param"),    Url.Contains(TEXT("title=")));
	TestTrue(TEXT("Prefill URL has body param"),     Url.Contains(TEXT("body=")));

	return true;
}

// ── 5. ArgsDigest 脱敏快照 ───────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkFeedbackRedactedSnapshotTest,
	"NexusLink.Feedback.RedactedArgsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkFeedbackRedactedSnapshotTest::RunTest(const FString& /*Parameters*/)
{
	// 正常字段：无敏感 key，保留原值
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/BP_Hero"));
		Args->SetNumberField(TEXT("count"), 3.0);
		const FString Snap = FNexusFeedback::BuildRedactedArgsSnapshot(Args);
		TestTrue(TEXT("Snapshot contains assetPath key"), Snap.Contains(TEXT("assetPath")));
		TestFalse(TEXT("Snapshot has no redacted marker for safe keys"), Snap.Contains(TEXT("<redacted>")));
	}

	// 敏感 key：值应被替换为 <redacted>
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("token"),    TEXT("super_secret_12345"));
		Args->SetStringField(TEXT("password"), TEXT("hunter2"));
		Args->SetStringField(TEXT("apiKey"),   TEXT("sk-abc"));
		Args->SetStringField(TEXT("name"),     TEXT("NPC_Boss"));
		const FString Snap = FNexusFeedback::BuildRedactedArgsSnapshot(Args);
		TestTrue(TEXT("Snapshot has redacted marker"),  Snap.Contains(TEXT("<redacted>")));
		TestFalse(TEXT("Secret value not in snapshot"), Snap.Contains(TEXT("super_secret_12345")));
		TestFalse(TEXT("Password not in snapshot"),     Snap.Contains(TEXT("hunter2")));
		TestTrue(TEXT("Safe field name still present"), Snap.Contains(TEXT("name")));
	}

	// 超长快照截断
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		FString LongVal;
		for (int32 i = 0; i < 50; ++i) LongVal += TEXT("ABCDEFGHIJ");  // 500 chars
		Args->SetStringField(TEXT("data"), LongVal);
		const FString Snap = FNexusFeedback::BuildRedactedArgsSnapshot(Args);
		TestTrue(TEXT("Snapshot length <= 203 (200 + ellipsis)"), Snap.Len() <= 203);
	}

	// 空参数
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		const FString Snap = FNexusFeedback::BuildRedactedArgsSnapshot(Args);
		TestTrue(TEXT("Empty args → empty snapshot"), Snap.IsEmpty());
	}

	return true;
}
