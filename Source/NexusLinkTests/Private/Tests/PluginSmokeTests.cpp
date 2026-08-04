// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpToolRegistry.h"

/**
 * 最小冒烟用例（SearchMode 架构）：
 * 1. NexusLink 模块已加载
 * 2. 插件元信息可见
 * 3. ToolRegistry 仅含 3 个元工具（call_capability / search_capabilities / submit_feedback）
 * 4. CapabilityRegistry 数量有下限，且若干关键 cap 名必须存在（防注册链断裂）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkPluginSmokeTest,
	"NexusLink.Smoke.PluginAndRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkPluginSmokeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("NexusLink module is loaded"),
		FModuleManager::Get().IsModuleLoaded(TEXT("NexusLink")));

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink"));
	TestTrue(TEXT("NexusLink plugin is discoverable"), Plugin.IsValid());
	if (Plugin.IsValid())
	{
		TestTrue(TEXT("NexusLink plugin is enabled"), Plugin->IsEnabled());
	}

	// ── SearchMode：ToolRegistry 只注册 3 个元工具 ──
	const TArray<FNexusMcpToolDefinition>& ToolDefs =
		FNexusMcpToolRegistry::Get().GetAllDefinitions();

	AddInfo(FString::Printf(TEXT("registered MCP tools: %d"), ToolDefs.Num()));
	TestEqual(TEXT("SearchMode meta tool count == 3"), ToolDefs.Num(), 3);

	TSet<FString> ToolNames;
	for (const FNexusMcpToolDefinition& Def : ToolDefs)
	{
		ToolNames.Add(Def.Name);
	}

	const TCHAR* MustExistTools[] = {
		TEXT("call_capability"),
		TEXT("search_capabilities"),
		TEXT("submit_feedback"),
	};
	for (const TCHAR* Name : MustExistTools)
	{
		TestTrue(FString::Printf(TEXT("meta tool %s must be registered"), Name),
			ToolNames.Contains(Name));
	}

	// ── Capability 注册表：业务能力在此，不在 ToolRegistry ──
	const TArray<FCapRecord>& CapRecords = FNexusCapabilityRegistry::Get().GetAllRecords();
	AddInfo(FString::Printf(TEXT("registered capabilities: %d"), CapRecords.Num()));
	// 留裕量：无 UnLua/GAS/StateTree/MVVM 时仍应远高于此；真实数由 AddInfo 打印
	TestTrue(TEXT("registered capability count >= 70"), CapRecords.Num() >= 70);

	TSet<FString> CapNames;
	for (const FCapRecord& Rec : CapRecords)
	{
		CapNames.Add(Rec.Def.Name);
	}

	const TCHAR* MustExistCaps[] = {
		TEXT("get_editor_info"),
		TEXT("get_output_log"),
		TEXT("search_asset"),
		TEXT("create_asset_blueprint"),
		TEXT("manage_asset_blueprint"),
		TEXT("save_asset"),
		TEXT("delete_asset"),
		TEXT("rename_asset"),
		TEXT("control_pie"),
		TEXT("list_runtime_actors"),
		TEXT("spawn_runtime_actor"),
		TEXT("destroy_runtime_actor"),
		TEXT("get_runtime_actor_property"),
		TEXT("set_runtime_actor_property"),
	};

	for (const TCHAR* Name : MustExistCaps)
	{
		TestTrue(FString::Printf(TEXT("capability %s must be registered"), Name),
			CapNames.Contains(Name));
	}

	return true;
}
