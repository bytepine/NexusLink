// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusRuntimeCapability.h"
#include "Utils/NexusHostUtils.h"

// ────────────────────────────────────────────────────────────────────────────
// 本地桩：不注册进全局表，仅测 GetHostScope / GetDefinition / 可见性
// ────────────────────────────────────────────────────────────────────────────

/** 默认基类 → EditorOnly；不写 runtime 标签。 */
class FNexusTestHostEditorCap : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("test_host_editor_only");
		Out.Description = TEXT("host-scope editor stub.");
		Out.InputSchema = MakeShared<FJsonObject>();
		Out.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Out.Tags        = { FNexusMcpTags::Editor };
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		return {};
	}
};

/** Runtime 基类且不手写 runtime 标签 → GetDefinition 应幂等补上。 */
class FNexusTestHostRuntimeCap : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("test_host_runtime");
		Out.Description = TEXT("host-scope runtime stub.");
		Out.InputSchema = MakeShared<FJsonObject>();
		Out.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Out.Tags        = { FNexusMcpTags::Readonly };
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		return {};
	}
};

/** Runtime 基类已手写 runtime 标签 → 不应重复追加。 */
class FNexusTestHostRuntimeTaggedCap : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("test_host_runtime_tagged");
		Out.Description = TEXT("host-scope runtime stub with tag.");
		Out.InputSchema = MakeShared<FJsonObject>();
		Out.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Out.Tags        = { FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		return {};
	}
};

static FCapRecord MakeCapRecord(TSharedRef<FNexusCapability> Cap)
{
	FCapRecord Record(Cap);
	Record.Def = Cap->GetDefinition();
	return Record;
}

// ────────────────────────────────────────────────────────────────────────────
// 1. 基类 HostScope + GetDefinition 幂等补 runtime 标签
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkHostScopeDefinitionTest,
	"NexusLink.Host.ScopeAndRuntimeTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkHostScopeDefinitionTest::RunTest(const FString& Parameters)
{
	FNexusTestHostEditorCap EditorCap;
	TestTrue(TEXT("default base → EditorOnly"),
		EditorCap.GetHostScope() == ENexusCapabilityHostScope::EditorOnly);
	TestFalse(TEXT("editor stub has no runtime tag"),
		EditorCap.GetDefinition().HasTag(FNexusMcpTags::Runtime));

	FNexusTestHostRuntimeCap RuntimeCap;
	TestTrue(TEXT("Runtime base → Runtime"),
		RuntimeCap.GetHostScope() == ENexusCapabilityHostScope::Runtime);
	const FNexusCapabilityDefinition& RuntimeDef = RuntimeCap.GetDefinition();
	TestTrue(TEXT("Runtime GetDefinition auto-adds runtime tag"),
		RuntimeDef.HasTag(FNexusMcpTags::Runtime));
	// 二次调用仍只有一个 runtime 标签
	int32 RuntimeTagCount = 0;
	for (const FString& Tag : RuntimeCap.GetDefinition().Tags)
	{
		if (Tag == FNexusMcpTags::Runtime)
		{
			++RuntimeTagCount;
		}
	}
	TestEqual(TEXT("runtime tag appears once after cache"), RuntimeTagCount, 1);

	FNexusTestHostRuntimeTaggedCap TaggedCap;
	int32 TaggedCount = 0;
	for (const FString& Tag : TaggedCap.GetDefinition().Tags)
	{
		if (Tag == FNexusMcpTags::Runtime)
		{
			++TaggedCount;
		}
	}
	TestEqual(TEXT("hand-written runtime tag is not duplicated"), TaggedCount, 1);

	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 2. IsCapabilityVisibleOnHost（含显式 bFullEditorHost 模拟 DS/Game）
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkHostVisibilityTest,
	"NexusLink.Host.Visibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkHostVisibilityTest::RunTest(const FString& Parameters)
{
	const FCapRecord EditorRec  = MakeCapRecord(MakeShared<FNexusTestHostEditorCap>());
	const FCapRecord RuntimeRec = MakeCapRecord(MakeShared<FNexusTestHostRuntimeCap>());

	// 完整 Editor：两类均可见
	TestTrue(TEXT("full editor: EditorOnly visible"),
		FNexusHostUtils::IsCapabilityVisibleOnHost(EditorRec, true));
	TestTrue(TEXT("full editor: Runtime visible"),
		FNexusHostUtils::IsCapabilityVisibleOnHost(RuntimeRec, true));

	// 非完整 Editor（DS / 纯 Game）：仅 Runtime
	TestFalse(TEXT("non-editor: EditorOnly hidden"),
		FNexusHostUtils::IsCapabilityVisibleOnHost(EditorRec, false));
	TestTrue(TEXT("non-editor: Runtime visible"),
		FNexusHostUtils::IsCapabilityVisibleOnHost(RuntimeRec, false));

	// 生产无参重载：本用例在 EditorContext，应为完整 Editor
	TestTrue(TEXT("IsFullEditorCapabilityHost in EditorContext"),
		FNexusHostUtils::IsFullEditorCapabilityHost());
	TestTrue(TEXT("no-arg: EditorOnly visible on full editor host"),
		FNexusHostUtils::IsCapabilityVisibleOnHost(EditorRec));
	TestTrue(TEXT("no-arg: Runtime visible on full editor host"),
		FNexusHostUtils::IsCapabilityVisibleOnHost(RuntimeRec));

	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 3. 注册表抽检：真实 Runtime / Editor-only cap 的 HostScope
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkHostRegistryScopeTest,
	"NexusLink.Host.RegistryScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkHostRegistryScopeTest::RunTest(const FString& Parameters)
{
	const FNexusCapabilityRegistry& Reg = FNexusCapabilityRegistry::Get();

	struct FExpect
	{
		const TCHAR* Name;
		ENexusCapabilityHostScope Scope;
	};
	const FExpect Expects[] = {
		{ TEXT("list_runtime_actors"), ENexusCapabilityHostScope::Runtime },
		{ TEXT("get_runtime_actor_animation"), ENexusCapabilityHostScope::Runtime },
		{ TEXT("get_editor_info"), ENexusCapabilityHostScope::EditorOnly },
		{ TEXT("search_asset"), ENexusCapabilityHostScope::EditorOnly },
	};

	for (const FExpect& E : Expects)
	{
		const FCapRecord* Rec = Reg.FindRecordByName(E.Name);
		if (!TestTrue(FString::Printf(TEXT("cap '%s' registered"), E.Name), Rec != nullptr))
		{
			continue;
		}
		TestTrue(
			FString::Printf(TEXT("cap '%s' HostScope"), E.Name),
			Rec->Instance->GetHostScope() == E.Scope);

		if (E.Scope == ENexusCapabilityHostScope::Runtime)
		{
			TestTrue(
				FString::Printf(TEXT("cap '%s' has runtime tag"), E.Name),
				Rec->Def.HasTag(FNexusMcpTags::Runtime));
			TestTrue(
				FString::Printf(TEXT("cap '%s' visible on non-editor host"), E.Name),
				FNexusHostUtils::IsCapabilityVisibleOnHost(*Rec, false));
		}
		else
		{
			TestFalse(
				FString::Printf(TEXT("cap '%s' hidden on non-editor host"), E.Name),
				FNexusHostUtils::IsCapabilityVisibleOnHost(*Rec, false));
		}
	}

	return true;
}
