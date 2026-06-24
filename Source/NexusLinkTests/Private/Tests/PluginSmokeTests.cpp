// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "NexusMcpToolRegistry.h"

/**
 * 最小冒烟用例：
 * 1. NexusLink 模块已加载
 * 2. 插件元信息可见
 * 3. 工具注册表至少有 39 个 Tool（留出裕量应对将来的实验性裁剪；真实数由 AddInfo 动态打印）
 * 4. 若干关键工具名必须存在，防止自动注册链断裂
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

	const TArray<FNexusMcpToolDefinition>& Defs =
		FNexusMcpToolRegistry::Get().GetAllDefinitions();

	AddInfo(FString::Printf(TEXT("registered MCP tools: %d"), Defs.Num()));
	TestTrue(TEXT("registered tool count >= 39"), Defs.Num() >= 39);

	TSet<FString> Names;
	for (const FNexusMcpToolDefinition& Def : Defs)
	{
		Names.Add(Def.Name);
	}

	const TCHAR* MustExist[] = {
		TEXT("call_capability"),
		TEXT("search_capabilities"),
		TEXT("get_editor_info"),
		TEXT("get_output_log"),
		TEXT("search_asset"),
		TEXT("get_asset"),
		TEXT("create_blueprint"),
		TEXT("manage_blueprint_variable"),
		TEXT("save_asset"),
		TEXT("delete_asset"),
		TEXT("rename_asset"),
		TEXT("control_pie"),
		TEXT("list_actors"),
		TEXT("spawn_actor"),
		TEXT("destroy_actor"),
		TEXT("get_property"),
		TEXT("set_property"),
	};

	for (const TCHAR* Name : MustExist)
	{
		TestTrue(FString::Printf(TEXT("tool %s must be registered"), Name),
			Names.Contains(Name));
	}

	return true;
}
