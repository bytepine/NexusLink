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
	// 运行时随 WITH_* 门控变化，不能断言 ==227；源码精确数走 nexus-unreal/Script/audit_capability_naming.py
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
		TEXT("get_asset_blueprint"),
		TEXT("manage_asset_blueprint"),
		TEXT("manage_asset_material"),
		TEXT("save_asset"),
		TEXT("delete_asset"),
		TEXT("rename_asset"),
		TEXT("control_pie"),
		TEXT("list_runtime_actors"),
		TEXT("spawn_runtime_actor"),
		TEXT("destroy_runtime_actor"),
		TEXT("get_runtime_actor_property"),
		TEXT("set_runtime_actor_property"),
		TEXT("create_asset_level_sequence"),
		TEXT("create_asset_physical_material"),
		TEXT("create_asset_sound_cue"),
		TEXT("create_asset_level"),
		TEXT("interact_runtime_actor_audio"),
		TEXT("interact_runtime_actor_ai"),
		TEXT("create_asset_string_table"),
		TEXT("get_asset_font"),
		TEXT("create_asset_foliage_type"),
		TEXT("create_asset_media_source"),
	};

	for (const TCHAR* Name : MustExistCaps)
	{
		TestTrue(FString::Printf(TEXT("capability %s must be registered"), Name),
			CapNames.Contains(Name));
	}

	return true;
}
