// Copyright byteyang. All Rights Reserved.

#include "NexusLink.h"
#include "Utils/NexusVersionCompat.h"
#include "Server/NexusMcpServer.h"
#include "NexusLinkSettings.h"
#include "Utils/NexusPortUtils.h"
#include "NexusInstanceRegistry.h"
#include "Editor/NexusLogCapture.h"
#include "Containers/Ticker.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#if WITH_EDITOR
#include "Editor/NexusEditorStatusBar.h"
#include "Editor/NexusLinkSettingsCustomization.h"
#include "PropertyEditorModule.h"
#endif

#define LOCTEXT_NAMESPACE "FNexusLinkModule"

DEFINE_LOG_CATEGORY_STATIC(LogNexusLink, Log, All);

#if NX_UE_HAS_FTSTICKER
using FNexusTicker = FTSTicker;
#else
using FNexusTicker = FTicker;
#endif

/**
 * 延迟一帧执行回调，避免在引擎初始化回调中直接打开模态对话框/通知。
 * 利用已有的 FTicker，无需额外模块依赖。
 */
static void CallNextTick(TFunction<void()> Callback)
{
	// 返回 false 让 Ticker 只触发一次后自动移除
	FNexusTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([Callback = MoveTemp(Callback)](float) -> bool
		{
			Callback();
			return false;
		}),
		0.0f
	);
}

void FNexusLinkModule::StartupModule()
{
	// 尽早注册日志捕获器，确保不遗漏启动阶段的日志
	LogCapture = MakeUnique<FNexusLogCapture>();
	LogCapture->Register();

#if WITH_EDITOR
	// 注册 Settings 自定义面板（按宿主 tags 分组的 Capability 树状列表）
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UNexusLinkSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FNexusLinkSettingsCustomization::MakeInstance));
#endif

#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FNexusLinkModule::OnPostEngineInit);
#else
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNexusLinkModule::OnPostEngineInit);
#endif
	UE_LOG(LogNexusLink, Log, TEXT("NexusLink 模块已加载，等待引擎初始化完成..."));
}

void FNexusLinkModule::ShutdownModule()
{
#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
#else
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
#endif

#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UNexusLinkSettings::StaticClass()->GetFName());
	}
#endif

	StopMcpServer();

	// 注销日志捕获器（析构时自动调用，此处显式提前注销）
	if (LogCapture.IsValid())
	{
		LogCapture->Unregister();
		LogCapture.Reset();
	}
}

void FNexusLinkModule::StopMcpServer()
{
#if WITH_EDITOR
	FNexusEditorStatusBar::UnregisterViewportStats();

	if (ToolbarExtender.IsValid())
	{
		FNexusEditorStatusBar::Unregister(ToolbarExtender);
		ToolbarExtender.Reset();
	}

	// McpPort/WsPort 为 Transient；编辑器退出时 UObject 可能已卸载，勿访问 Settings
	if (!IsEngineExitRequested())
	{
		if (UNexusLinkSettings* MutableSettings = UNexusLinkSettings::Get())
		{
			MutableSettings->McpPort = 0;
			MutableSettings->WsPort  = 0;
		}
	}
#endif

	if (McpServer.IsValid())
	{
		FNexusInstanceRegistry::Unregister();

		McpServer->Stop();
		McpServer.Reset();
		UE_LOG(LogNexusLink, Log, TEXT("NexusLink MCP 服务器已停止"));
	}
}

bool FNexusLinkModule::TryStartMcpServer()
{
	if (McpServer.IsValid() && McpServer->IsRunning())
	{
		return true;
	}

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!Settings || !Settings->bEnableMcpServer)
	{
		return false;
	}

	// 端口从默认值开始自动寻找可用端口，冲突时向上顺延（无需用户手动配置）
	constexpr int32 DefaultMcpPort  = 45000;
	constexpr int32 DefaultWsPort   = 55000;
	constexpr int32 MaxStartRetries = 3;

	// 读取其他活跃实例已占用的端口，避免 bind 探测与实际监听之间的 TOCTOU 竞态
	TArray<int32> ExcludePorts = FNexusInstanceRegistry::GetClaimedPorts();

	int32 ActualMcpPort = -1;
	int32 ActualWsPort  = -1;
	bool bStarted = false;

	for (int32 Attempt = 0; Attempt < MaxStartRetries; ++Attempt)
	{
		ActualMcpPort = FNexusPortUtils::FindAvailablePort(DefaultMcpPort, ExcludePorts);
		if (ActualMcpPort == -1)
		{
			UE_LOG(LogNexusLink, Error, TEXT("MCP 端口 %d 起始范围内无可用端口"), DefaultMcpPort);
			return false;
		}
		ExcludePorts.AddUnique(ActualMcpPort);

		ActualWsPort = FNexusPortUtils::FindAvailablePort(DefaultWsPort, ExcludePorts);
		if (ActualWsPort == -1)
		{
			UE_LOG(LogNexusLink, Error, TEXT("WebSocket 端口 %d 起始范围内无可用端口"), DefaultWsPort);
			return false;
		}

		McpServer = MakeShared<FNexusMcpServer>();
		if (McpServer->Start(ActualMcpPort, ActualWsPort))
		{
			bStarted = true;
			break;
		}

		UE_LOG(LogNexusLink, Warning, TEXT("服务器启动失败（MCP: %d, WS: %d），重试第 %d 次..."),
			ActualMcpPort, ActualWsPort, Attempt + 1);
		McpServer.Reset();
		ExcludePorts.AddUnique(ActualWsPort);
	}

	if (!bStarted)
	{
		UE_LOG(LogNexusLink, Error, TEXT("NexusLink 服务器启动失败，已重试 %d 次"), MaxStartRetries);
		McpServer.Reset();
		return false;
	}

	UE_LOG(LogNexusLink, Log, TEXT("NexusLink 服务器已启动，MCP: http://127.0.0.1:%d/mcp，WS: ws://127.0.0.1:%d/"), ActualMcpPort, ActualWsPort);

	// 向临时目录写入注册文件，供 Rider 等客户端无需端口扫描即可发现本实例
	FNexusInstanceRegistry::Register(
		ActualMcpPort,
		ActualWsPort,
		FString(FApp::GetProjectName()),
		FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION)
	);

#if WITH_EDITOR
	// 将实际运行端口回写到设置对象，供设置面板只读显示（Transient，不持久化）
	// 同时注册工具栏状态组件（需延迟一帧，确保 Level Editor 已完成初始化）
	CallNextTick([this, ActualMcpPort, ActualWsPort]()
	{
		if (IsEngineExitRequested())
		{
			return;
		}
		if (UNexusLinkSettings* MutableSettings = UNexusLinkSettings::Get())
		{
			MutableSettings->McpPort = ActualMcpPort;
			MutableSettings->WsPort  = ActualWsPort;
		}
		ToolbarExtender = FNexusEditorStatusBar::Register(ActualMcpPort, ActualWsPort);
		FNexusEditorStatusBar::RegisterViewportStats(ActualMcpPort, ActualWsPort);
	});
#endif

	return true;
}

void FNexusLinkModule::OnPostEngineInit()
{
	// 把当前已注册的 Capability 全部纳入 KnownCapabilityKeys（首次启动默认全部启用）
	UNexusLinkSettings::Get()->EnsureDefaultCapabilityMode();

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (LogCapture.IsValid())
	{
		LogCapture->SetCategoryWhitelist(Settings->LogCaptureCategories);
	}

	if (!Settings->bEnableMcpServer)
	{
		UE_LOG(LogNexusLink, Log, TEXT("MCP 服务器未启用，可在 Editor Preferences → Plugins → NexusLink 中开启"));
		return;
	}

	TryStartMcpServer();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNexusLinkModule, NexusLink)
