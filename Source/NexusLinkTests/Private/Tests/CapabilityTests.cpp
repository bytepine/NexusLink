// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"

// ────────────────────────────────────────────────────────────────────────────
// 测试辅助 Capability/Tool 子类（遵项目规范 §2.5 禁用 namespace）
// ────────────────────────────────────────────────────────────────────────────

/** 实现 BuildDefinition 的测试 Capability，用于验证 GetDefinition 能正确拼装元数据。 */
class FNexusTestMetadataCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("MetaCap");
		Out.Description = TEXT("desc-text.");
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		Out.InputSchema = S;
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		return {};
	}
};

/** 未重写 Execute 的最小工具，用于验证默认兜底报错。 */
class FNexusTestBareMinimumTool : public FNexusMcpTool
{
protected:
	virtual void BuildDefinition(FNexusMcpToolDefinition& Out) const override
	{
		Out.Name        = TEXT("test_bare_minimum");
		Out.Description = TEXT("bare.");
	}
};

// ────────────────────────────────────────────────────────────────────────────
// 0. GetDefinition：拼装 Name / Description / InputSchema 的元数据（实例级 lazy 缓存）
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilityGetDefinitionTest,
	"NexusLink.Capability.GetDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilityGetDefinitionTest::RunTest(const FString& Parameters)
{
	FNexusTestMetadataCapability Cap;
	const FNexusCapabilityDefinition& Def1 = Cap.GetDefinition();
	TestEqual(TEXT("Definition.Name"),        Def1.Name,        FString(TEXT("MetaCap")));
	TestEqual(TEXT("Definition.Description"), Def1.Description, FString(TEXT("desc-text.")));
	TestTrue (TEXT("Definition.InputSchema present"), Def1.InputSchema.IsValid());
	if (Def1.InputSchema.IsValid())
	{
		TestEqual(TEXT("schema.type"),
			Def1.InputSchema->GetStringField(TEXT("type")), FString(TEXT("object")));
	}

	// 实例级 lazy 缓存：第二次调用返回同一个对象（地址不变）
	const FNexusCapabilityDefinition& Def2 = Cap.GetDefinition();
	TestTrue(TEXT("Cached: same ptr"), &Def1 == &Def2);

	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 1. 默认 Execute 兜底：未重写 Execute → 正确报错
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkToolDefaultExecuteTest,
	"NexusLink.Capability.ToolDefaultExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkToolDefaultExecuteTest::RunTest(const FString& Parameters)
{
	FNexusTestBareMinimumTool Tool;
	const FNexusMcpToolResult R = Tool.Execute(MakeShared<FJsonObject>());
	TestTrue(TEXT("bare tool → bIsError"), R.bIsError);
	TestFalse(TEXT("error text non-empty"), R.ErrorText.IsEmpty());
	return true;
}
