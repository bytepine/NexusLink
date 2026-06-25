// Copyright byteyang. All Rights Reserved.

/**
 * StateTree + MVVM Capability 单元测试
 *
 * 覆盖范围（无需运行中的 UE 编辑器 / 真实资产）：
 *   1. 元数据：BuildDefinition 返回正确的 Name / Tags / Schema
 *   2. 注册表：WITH_STATETREE/WITH_MVVM 时 capability 在全局注册表中可查到
 *   3. 参数校验：Execute 缺 assetPath → 返回错误而非崩溃
 *   4. 不存在资产：Execute 给无效路径 → 返回错误而非崩溃
 *   5. search_asset 类型归一：statetree/st/mvvm/viewmodel 缩写归一验证
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"

// ── 辅助：构建测试用 Arguments ────────────────────────────────────────────────

static TSharedPtr<FJsonObject> MakeArgs()
{
	return MakeShared<FJsonObject>();
}

static TSharedPtr<FJsonObject> MakeArgs(const TCHAR* Key, const FString& Val)
{
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(Key, Val);
	return Args;
}

// ── 辅助：在注册表中按名查找 ──────────────────────────────────────────────────

static bool IsCapabilityRegistered(const TCHAR* CapName)
{
	return FNexusCapabilityRegistry::Get().FindRecordByName(CapName) != nullptr;
}

// ── 辅助：判断 FCapabilityResult 是否携带致命错误 ─────────────────────────────

#include "NexusCapability.h"

static bool ResultHasError(const FCapabilityResult& R)
{
	return !R.FatalError.IsEmpty();
}

static const FString& ResultErrorText(const FCapabilityResult& R)
{
	return R.FatalError;
}

// ═══════════════════════════════════════════════════════════════════════════════
// §1  get_asset_state_tree — 元数据 / 注册表 / 参数校验
// ═══════════════════════════════════════════════════════════════════════════════

#if WITH_STATETREE

#include "Capabilities/Asset/StateTree/NexusGetAssetStateTreeCapability.h"

// ─── 1a. 元数据 ───────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusStateTreeCapDefinitionTest,
	"NexusLink.StateTree.Capability.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStateTreeCapDefinitionTest::RunTest(const FString& Parameters)
{
	FGetAssetStateTreeCapability Cap;
	const FNexusCapabilityDefinition& Def = Cap.GetDefinition();

	TestEqual(TEXT("Name == get_asset_state_tree"),
		Def.Name, FString(TEXT("get_asset_state_tree")));
	TestFalse(TEXT("Description non-empty"), Def.Description.IsEmpty());
	TestTrue (TEXT("InputSchema present"), Def.InputSchema.IsValid());

	// Tags 包含 Readonly
	bool bHasReadonly = false;
	for (const FString& Tag : Def.Tags)
	{
		if (Tag == FNexusMcpTags::Readonly)
		{
			bHasReadonly = true;
			break;
		}
	}
	TestTrue(TEXT("Tags contains Readonly"), bHasReadonly);

	// RelatedCapabilities 不为空
	TestFalse(TEXT("RelatedCapabilities non-empty"), Def.RelatedCapabilities.IsEmpty());

	return true;
}

// ─── 1b. 注册表 ───────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusStateTreeCapRegistryTest,
	"NexusLink.StateTree.Capability.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStateTreeCapRegistryTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("get_asset_state_tree registered in global registry"),
		IsCapabilityRegistered(TEXT("get_asset_state_tree")));
	return true;
}

// ─── 1c. 参数校验：缺少 assetPath ────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusStateTreeCapMissingPathTest,
	"NexusLink.StateTree.Capability.MissingAssetPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStateTreeCapMissingPathTest::RunTest(const FString& Parameters)
{
	FGetAssetStateTreeCapability Cap;

	// 空参数对象 → 应返回错误（通过公开 Run() 调用）
	const FCapabilityResult R1 = Cap.Run(MakeArgs());
	TestTrue(TEXT("empty args → error"), ResultHasError(R1));

	// assetPath 为空串 → 应返回错误
	const FCapabilityResult R2 = Cap.Run(MakeArgs(TEXT("assetPath"), TEXT("")));
	TestTrue(TEXT("empty assetPath → error"), ResultHasError(R2));

	return true;
}

// ─── 1d. 无效资产路径：不崩溃，返回错误 ─────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusStateTreeCapInvalidPathTest,
	"NexusLink.StateTree.Capability.InvalidAssetPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStateTreeCapInvalidPathTest::RunTest(const FString& Parameters)
{
	FGetAssetStateTreeCapability Cap;

	const FCapabilityResult R = Cap.Run(
		MakeArgs(TEXT("assetPath"), TEXT("/Game/__DoesNotExist_StateTree_XYZ__")));

	// 不应崩溃；应报告资产未找到
	TestTrue(TEXT("invalid path → error (no crash)"), ResultHasError(R));

	return true;
}

#endif // WITH_STATETREE

// ═══════════════════════════════════════════════════════════════════════════════
// §2  get_asset_view_model — 元数据 / 注册表 / 参数校验
// ═══════════════════════════════════════════════════════════════════════════════

#if WITH_MVVM

#include "Capabilities/Asset/MVVM/NexusGetAssetViewModelCapability.h"

// ─── 2a. 元数据 ───────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusMvvmCapDefinitionTest,
	"NexusLink.MVVM.Capability.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMvvmCapDefinitionTest::RunTest(const FString& Parameters)
{
	FGetAssetViewModelCapability Cap;
	const FNexusCapabilityDefinition& Def = Cap.GetDefinition();

	TestEqual(TEXT("Name == get_asset_view_model"),
		Def.Name, FString(TEXT("get_asset_view_model")));
	TestFalse(TEXT("Description non-empty"), Def.Description.IsEmpty());
	TestTrue (TEXT("InputSchema present"), Def.InputSchema.IsValid());

	// Tags 包含 Readonly
	bool bHasReadonly = false;
	for (const FString& Tag : Def.Tags)
	{
		if (Tag == FNexusMcpTags::Readonly)
		{
			bHasReadonly = true;
			break;
		}
	}
	TestTrue(TEXT("Tags contains Readonly"), bHasReadonly);

	// Tags 包含 Widget
	bool bHasWidget = false;
	for (const FString& Tag : Def.Tags)
	{
		if (Tag == FNexusMcpTags::Widget)
		{
			bHasWidget = true;
			break;
		}
	}
	TestTrue(TEXT("Tags contains Widget"), bHasWidget);

	return true;
}

// ─── 2b. 注册表 ───────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusMvvmCapRegistryTest,
	"NexusLink.MVVM.Capability.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMvvmCapRegistryTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("get_asset_view_model registered in global registry"),
		IsCapabilityRegistered(TEXT("get_asset_view_model")));
	return true;
}

// ─── 2c. 参数校验：缺少 assetPath ────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusMvvmCapMissingPathTest,
	"NexusLink.MVVM.Capability.MissingAssetPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMvvmCapMissingPathTest::RunTest(const FString& Parameters)
{
	FGetAssetViewModelCapability Cap;

	// 空参数 → 错误（通过公开 Run() 调用）
	const FCapabilityResult R1 = Cap.Run(MakeArgs());
	TestTrue(TEXT("empty args → error"), ResultHasError(R1));

	// 空路径 → 错误
	const FCapabilityResult R2 = Cap.Run(MakeArgs(TEXT("assetPath"), TEXT("")));
	TestTrue(TEXT("empty assetPath → error"), ResultHasError(R2));

	return true;
}

// ─── 2d. 无效资产路径：不崩溃，返回错误 ─────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusMvvmCapInvalidPathTest,
	"NexusLink.MVVM.Capability.InvalidAssetPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMvvmCapInvalidPathTest::RunTest(const FString& Parameters)
{
	FGetAssetViewModelCapability Cap;

	const FCapabilityResult R = Cap.Run(
		MakeArgs(TEXT("assetPath"), TEXT("/Game/__DoesNotExist_WBP_XYZ__")));

	TestTrue(TEXT("invalid path → error (no crash)"), ResultHasError(R));

	return true;
}

#endif // WITH_MVVM

// ═══════════════════════════════════════════════════════════════════════════════
// §3  search_asset 类型归一：NormalizeAssetTypeShortcut 行为（纯逻辑，无资产加载）
//     通过注册表 Run() 传入不同 assetType + 极窄 pathFilter，验证不崩溃且无"Unknown type"错误
// ═══════════════════════════════════════════════════════════════════════════════

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusSearchAssetStateTreeShortcutTest,
	"NexusLink.Search.AssetType.StateTreeShortcut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSearchAssetStateTreeShortcutTest::RunTest(const FString& Parameters)
{
	// 通过注册表获取 search_asset capability 实例（避免直接实例化 protected Execute 问题）
	const FCapRecord* Record = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("search_asset"));
	if (!Record)
	{
		AddError(TEXT("search_asset not found in registry — cannot run shortcut tests"));
		return false;
	}

	// 用极窄不存在的 pathFilter 保证不扫全项目；关注的是不崩溃 + 不返回"Unknown type"错误
	auto RunSearch = [&](const TCHAR* AssetType) -> FCapabilityResult
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetType"),  AssetType);
		Args->SetStringField(TEXT("pathFilter"), TEXT("/Game/__NexusTest_DoesNotExist__/"));
		Args->SetStringField(TEXT("nameFilter"), TEXT("__DoesNotExist__"));
		return Record->Instance->Run(Args);
	};

#if WITH_STATETREE
	{
		// "statetree" shortcut 应被识别（不返回 Unknown assetType 错误）
		const FCapabilityResult R = RunSearch(TEXT("statetree"));
		// 结果为空列表或成功；不应为 "Unknown assetType" 这类 argInvalid 错误
		// 只要不 crash 且 errorKind 不是 arg_invalid 即通过
		if (ResultHasError(R))
		{
			// 如果确实报错，确保不是"Unknown assetType"报错（即归一化生效了）
			TestFalse(TEXT("statetree shortcut should not produce 'Unknown assetType' error"),
				ResultErrorText(R).Contains(TEXT("Unknown assetType")));
		}
	}
	{
		const FCapabilityResult R = RunSearch(TEXT("st"));
		if (ResultHasError(R))
		{
			TestFalse(TEXT("st shortcut should not produce 'Unknown assetType' error"),
				ResultErrorText(R).Contains(TEXT("Unknown assetType")));
		}
	}
#endif // WITH_STATETREE

	{
		// "widget" shortcut 始终有效（已有），"viewmodel" 应归一到 widget
		const FCapabilityResult R = RunSearch(TEXT("viewmodel"));
		if (ResultHasError(R))
		{
			TestFalse(TEXT("viewmodel shortcut should not produce 'Unknown assetType' error"),
				ResultErrorText(R).Contains(TEXT("Unknown assetType")));
		}
	}
	{
		const FCapabilityResult R = RunSearch(TEXT("mvvm"));
		if (ResultHasError(R))
		{
			TestFalse(TEXT("mvvm shortcut should not produce 'Unknown assetType' error"),
				ResultErrorText(R).Contains(TEXT("Unknown assetType")));
		}
	}

	return true;
}
