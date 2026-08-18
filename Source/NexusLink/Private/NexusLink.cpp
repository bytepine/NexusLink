// Copyright byteyang. All Rights Reserved.

#include "NexusLink.h"
#include "Utils/NexusVersionCompat.h"
#include "Server/NexusMcpServer.h"
#include "NexusLinkSettings.h"
#include "NexusMcpToolRegistry.h"
#include "Utils/NexusPortUtils.h"
#include "NexusInstanceRegistry.h"
#include "Editor/NexusLogCapture.h"
#include "Containers/Ticker.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#if WITH_EDITOR
#include "Editor/NexusEditorStatusBar.h"
#include "Editor/NexusLinkSettingsCustomization.h"
#include "NexusUpdateChecker.h"
#include "PropertyEditorModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
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

/**
 * Preferences 勾选或命令行 -EnableNexusMcp 任一为真即请求启动 MCP。
 * CLI 仅本进程会话生效，不改 bEnableMcpServer、不 SaveConfig。
 * 控制台 NexusLink.EnableMcp 不经此函数，直接调 TryStart/Stop。
 */
static bool IsMcpServerRequestedAtStartup()
{
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (Settings && Settings->bEnableMcpServer)
	{
		return true;
	}
	return FParse::Param(FCommandLine::Get(), TEXT("EnableNexusMcp"));
}

void FNexusLinkModule::StartupModule()
{
#if !WITH_EDITOR
	// Type=Runtime 以便 Game/Server 目标可链接；非编辑器构建不做任何初始化
	UE_LOG(LogNexusLink, Log, TEXT("非编辑器构建：NexusLink 不启动（MCP 仅 Editor / PIE）"));
	return;
#else

	// 尽早注册日志捕获器，确保不遗漏启动阶段的日志
	LogCapture = MakeUnique<FNexusLogCapture>();
	LogCapture->Register();

	// 注册 Settings 自定义面板（按宿主 tags 分组的 Capability 树状列表）
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UNexusLinkSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FNexusLinkSettingsCustomization::MakeInstance));

#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FNexusLinkModule::OnPostEngineInit);
#else
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNexusLinkModule::OnPostEngineInit);
#endif

	EnableMcpConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("NexusLink.EnableMcp"),
		HELP_TEXT("会话级启停 MCP（不写 Preferences）。用法: NexusLink.EnableMcp 1|0；无参数打印当前状态。仅编辑器有效。"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FNexusLinkModule::HandleEnableMcpCommand),
		ECVF_Default);

	UE_LOG(LogNexusLink, Log, TEXT("NexusLink 模块已加载，等待引擎初始化完成..."));

	// 静态初始化期禁止 UE_LOG，注册表把诊断信息缓存下来，此处统一输出
	for (const FString& Warning : FNexusMcpToolRegistry::Get().GetPendingWarnings())
	{
		UE_LOG(LogNexusLink, Warning, TEXT("%s"), *Warning);
	}
#endif // WITH_EDITOR
}

void FNexusLinkModule::ShutdownModule()
{
#if !WITH_EDITOR
	// 与 StartupModule 对称：非编辑器构建未初始化，直接返回
	return;
#else

#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
#else
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
#endif

	if (EnableMcpConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(EnableMcpConsoleCommand);
		EnableMcpConsoleCommand = nullptr;
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UNexusLinkSettings::StaticClass()->GetFName());
	}

	StopMcpServer();

	// 注销日志捕获器（析构时自动调用，此处显式提前注销）
	if (LogCapture.IsValid())
	{
		LogCapture->Unregister();
		LogCapture.Reset();
	}
#endif // WITH_EDITOR
}

void FNexusLinkModule::StopMcpServer()
{
#if WITH_EDITOR
	FNexusEditorStatusBar::Unregister();

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
	// 同时在主窗口菜单栏注册端口指示（延迟一帧，确保主窗口已创建）
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
		FNexusEditorStatusBar::Register(ActualMcpPort, ActualWsPort);
	});
#endif

	return true;
}

void FNexusLinkModule::OnPostEngineInit()
{
	// 把当前已注册的 Capability 全部纳入 KnownCapabilityKeys（首次启动默认全部启用）
	UNexusLinkSettings::Get()->EnsureDefaultCapabilityMode();
	// 首次 / 升级：空白名单写入诊断默认集，避免全量捕获冲刷环形缓冲
	UNexusLinkSettings::Get()->EnsureLogCaptureDefaults();

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (LogCapture.IsValid())
	{
		LogCapture->SetCategoryWhitelist(Settings->LogCaptureCategories);
	}

	if (!IsMcpServerRequestedAtStartup())
	{
		UE_LOG(LogNexusLink, Log,
			TEXT("MCP 服务器未启用。可在 Preferences 勾选、启动参数 -EnableNexusMcp，或控制台 NexusLink.EnableMcp 1"));
	}
	else
	{
		TryStartMcpServer();
	}

#if WITH_EDITOR
	// 每会话启动时静默检查一次版本更新；仅当有新版本时弹出非阻塞通知
	static bool bVersionChecked = false;
	if (!bVersionChecked && Settings->bCheckUpdateOnStartup)
	{
		bVersionChecked = true;
		CallNextTick([]()
		{
			FNexusUpdateChecker::CheckAsync(
				[](bool bHasUpdate, FString LatestVersion, FString CurrentVersion)
				{
					if (!bHasUpdate)
					{
						return;
					}
					FNotificationInfo Info(FText::FromString(
						FString::Printf(TEXT("NexusLink 有新版本可用：%s（当前 %s）"),
							*LatestVersion, *CurrentVersion)));
					Info.bFireAndForget = true;
					Info.ExpireDuration = 10.0f;
					Info.bUseSuccessFailIcons = true;
					Info.bUseLargeFont = false;
					Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
					{
						FPlatformProcess::LaunchURL(
							TEXT("https://github.com/bytepine/NexusLink/releases"),
							nullptr, nullptr);
					});
					Info.HyperlinkText = LOCTEXT("UpdateNotifLink", "查看 Releases 页面");
					FSlateNotificationManager::Get().AddNotification(Info)
						->SetCompletionState(SNotificationItem::CS_Success);
				}
			);
		});
	}
#endif
}

void FNexusLinkModule::HandleEnableMcpCommand(const TArray<FString>& Args)
{
	const bool bRunning = McpServer.IsValid() && McpServer->IsRunning();
	if (Args.Num() < 1)
	{
		UE_LOG(LogNexusLink, Log, TEXT("NexusLink.EnableMcp 当前=%s（用法: NexusLink.EnableMcp 1|0）"),
			bRunning ? TEXT("on") : TEXT("off"));
		return;
	}

	const FString& Arg0 = Args[0];
	const bool bEnable =
		Arg0 == TEXT("1")
		|| Arg0.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| Arg0.Equals(TEXT("on"), ESearchCase::IgnoreCase);

	const bool bDisable =
		Arg0 == TEXT("0")
		|| Arg0.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		|| Arg0.Equals(TEXT("off"), ESearchCase::IgnoreCase);

	if (!bEnable && !bDisable)
	{
		UE_LOG(LogNexusLink, Warning, TEXT("NexusLink.EnableMcp 参数无效 '%s'（期望 1|0）"), *Arg0);
		return;
	}

	if (bEnable)
	{
		if (TryStartMcpServer())
		{
			UE_LOG(LogNexusLink, Log, TEXT("NexusLink.EnableMcp: MCP 已开启（会话级，未写 Preferences）"));
		}
		else
		{
			UE_LOG(LogNexusLink, Error, TEXT("NexusLink.EnableMcp: 启动失败"));
		}
	}
	else
	{
		StopMcpServer();
		UE_LOG(LogNexusLink, Log, TEXT("NexusLink.EnableMcp: MCP 已关闭（会话级，未写 Preferences）"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNexusLinkModule, NexusLink)
