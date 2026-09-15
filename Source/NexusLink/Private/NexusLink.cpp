// Copyright byteyang. All Rights Reserved.

#include "NexusLink.h"
#include "NexusLinkBuildConfig.h"
#include "Utils/NexusVersionCompat.h"
#include "Server/NexusMcpServer.h"
#include "NexusLinkSettings.h"
#include "NexusMcpToolRegistry.h"
#include "Utils/NexusPortUtils.h"
#include "NexusInstanceRegistry.h"
#include "Log/NexusLogCapture.h"
#include "Containers/Ticker.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Engine/World.h"
#include "NexusEditorServices.h"

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
	// 尽早注册日志捕获器，确保不遗漏启动阶段的日志（Game/DS -server 无 UI 也要捕获）
	LogCapture = MakeUnique<FNexusLogCapture>();
	LogCapture->Register();

	// PropertyEditor 设置面板定制、状态栏、更新检查通知均为编辑器专属 UI，
	// 由 NexusLinkEditor 模块的 FNexusLinkEditorModule::StartupModule 负责注册
	// （该模块链接本模块后启动，安装 FNexusEditorServices 钩子）。

#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FNexusLinkModule::OnPostEngineInit);
#else
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNexusLinkModule::OnPostEngineInit);
#endif

	EnableMcpConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("NexusLink.EnableMcp"),
		HELP_TEXT("会话级启停 MCP（不写 Preferences）。独立 Game 包按 ~ 打开控制台即可，效果同 -EnableNexusMcp。用法: NexusLink.EnableMcp 1|0 [Port=] [WsPort=] [Lan=1|-NexusAllowLan]；无参数打印状态与监听地址。"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FNexusLinkModule::HandleEnableMcpCommand),
		ECVF_Default);

	UE_LOG(LogNexusLink, Log, TEXT("NexusLink 模块已加载，等待引擎初始化完成..."));

	// 静态初始化期禁止 UE_LOG，注册表把诊断信息缓存下来，此处统一输出
	for (const FString& Warning : FNexusMcpToolRegistry::Get().GetPendingWarnings())
	{
		UE_LOG(LogNexusLink, Warning, TEXT("%s"), *Warning);
	}
}

void FNexusLinkModule::ShutdownModule()
{
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
	// 状态栏是编辑器专属 UI，通过钩子反向通知（Runtime 无钩子实现时为安全空操作）
	FNexusEditorServices::NotifyServerStopped();

	// McpPort/WsPort 为 Transient；编辑器退出时 UObject 可能已卸载，勿访问 Settings
	if (!IsEngineExitRequested())
	{
		if (UNexusLinkSettings* MutableSettings = UNexusLinkSettings::Get())
		{
			MutableSettings->McpPort = 0;
			MutableSettings->WsPort  = 0;
			// 本机 token 文件仍在，设置面板继续展示以便跨机复制
		}
	}

#if NEXUSLINK_WITH_SERVER
	if (McpServer.IsValid())
	{
		FNexusInstanceRegistry::Unregister();

		McpServer->Stop();
		McpServer.Reset();
		UE_LOG(LogNexusLink, Log, TEXT("NexusLink MCP 服务器已停止"));
	}
#endif
}

bool FNexusLinkModule::TryStartMcpServer()
{
#if !NEXUSLINK_WITH_SERVER
	// Shipping 编译期剔除服务器（NEXUSLINK_WITH_SERVER=0）：HTTP/HTTPServer/WebSocketNetworking
	// 未链接，FNexusMcpServer 仅剩前向声明，此路径直接短路，不触碰该不完整类型。
	UE_LOG(LogNexusLink, Warning, TEXT("Shipping 配置不带 MCP 服务器（编译期剔除）"));
	return false;
#else
	if (McpServer.IsValid() && McpServer->IsRunning())
	{
		return true;
	}

	// 端口：控制台会话覆盖 > -NexusMcpPort= / -NexusWsPort= > 默认 45000 / 55000；冲突时向上顺延
	constexpr int32 FallbackMcpPort = 45000;
	constexpr int32 FallbackWsPort  = 55000;
	constexpr int32 MaxStartRetries = 3;

	auto ResolveStartPort = [](int32 SessionOverride, const TCHAR* CliKey, int32 Fallback) -> int32
	{
		if (SessionOverride > 0 && SessionOverride <= 65535)
		{
			return SessionOverride;
		}
		int32 CliPort = 0;
		if (FParse::Value(FCommandLine::Get(), CliKey, CliPort) && CliPort > 0 && CliPort <= 65535)
		{
			return CliPort;
		}
		return Fallback;
	};
	const int32 DefaultMcpPort = ResolveStartPort(SessionMcpPort, TEXT("NexusMcpPort="), FallbackMcpPort);
	const int32 DefaultWsPort  = ResolveStartPort(SessionWsPort, TEXT("NexusWsPort="), FallbackWsPort);

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
		FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION),
		McpServer->GetAuthToken()
	);

	// 将实际运行端口回写到设置对象，供设置面板只读显示（Transient，不持久化）
	// 状态栏是编辑器专属 UI，通过钩子反向通知（Runtime 无钩子实现时为安全空操作）
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
			if (McpServer.IsValid())
			{
				MutableSettings->McpAuthToken = McpServer->GetAuthToken();
			}
		}
		FNexusEditorServices::NotifyServerStarted(ActualMcpPort, ActualWsPort);
	});

	return true;
#endif // NEXUSLINK_WITH_SERVER
}

void FNexusLinkModule::OnPostEngineInit()
{
	// 把当前已注册的 Capability 全部纳入 KnownCapabilityKeys（首次启动默认全部启用）
	UNexusLinkSettings::Get()->EnsureDefaultCapabilityMode();
	UNexusLinkSettings::Get()->EnsureDangerousCapsDefaultOff();
	if (FParse::Param(FCommandLine::Get(), TEXT("NexusEnableDangerousCaps")))
	{
		UNexusLinkSettings::Get()->EnableDangerousCapsForSession();
	}
	UNexusLinkSettings::Get()->EnsureLogCaptureDefaults();

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (LogCapture.IsValid())
	{
		LogCapture->SetCategoryWhitelist(Settings->LogCaptureCategories);
	}

	if (!IsMcpServerRequestedAtStartup())
	{
		UE_LOG(LogNexusLink, Log,
			TEXT("MCP 服务器未启用。可在 Preferences 勾选、启动参数 -EnableNexusMcp，或运行时控制台 NexusLink.EnableMcp 1"));
	}
	else
	{
		TryStartMcpServer();
	}

	// 版本更新检查通知是编辑器专属 UI，由 FNexusLinkEditorModule::OnPostEngineInit 负责
}

static bool ParseEnableToken(const FString& Token, bool& bEnable, bool& bDisable)
{
	bEnable =
		Token == TEXT("1")
		|| Token.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| Token.Equals(TEXT("on"), ESearchCase::IgnoreCase);
	bDisable =
		Token == TEXT("0")
		|| Token.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		|| Token.Equals(TEXT("off"), ESearchCase::IgnoreCase);
	return bEnable || bDisable;
}

/** 解析 Port=/WsPort=/Lan= 以及与启动参数同形的 -NexusMcpPort= / -NexusWsPort= / -NexusAllowLan。 */
static bool ApplyListenOverridesFromArgs(
	const TArray<FString>& Args,
	int32 FirstIndex,
	int32& OutMcpPort,
	int32& OutWsPort,
	int8& OutLan)
{
	bool bAny = false;
	for (int32 i = FirstIndex; i < Args.Num(); ++i)
	{
		const FString& Token = Args[i];
		int32 Parsed = 0;
		// WsPort 必须先于 Port，避免 "WsPort=x" 被 "Port=" 误吃
		if (FParse::Value(*Token, TEXT("NexusWsPort="), Parsed)
			|| FParse::Value(*Token, TEXT("WsPort="), Parsed))
		{
			OutWsPort = Parsed;
			bAny = true;
			continue;
		}
		if (FParse::Value(*Token, TEXT("NexusMcpPort="), Parsed)
			|| FParse::Value(*Token, TEXT("Port="), Parsed))
		{
			OutMcpPort = Parsed;
			bAny = true;
			continue;
		}
		if (Token.Equals(TEXT("-NexusAllowLan"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("NexusAllowLan"), ESearchCase::IgnoreCase))
		{
			OutLan = 1;
			bAny = true;
			continue;
		}
		FString LanValue;
		if (FParse::Value(*Token, TEXT("Lan="), LanValue) && !LanValue.IsEmpty())
		{
			const bool bLanOn =
				LanValue == TEXT("1")
				|| LanValue.Equals(TEXT("true"), ESearchCase::IgnoreCase)
				|| LanValue.Equals(TEXT("on"), ESearchCase::IgnoreCase);
			OutLan = bLanOn ? 1 : 0;
			bAny = true;
		}
	}
	return bAny;
}

void FNexusLinkModule::HandleEnableMcpCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	(void)World;
#if !NEXUSLINK_WITH_SERVER
	Ar.Log(TEXT("Shipping 配置不带 MCP 服务器（编译期剔除）"));
	UE_LOG(LogNexusLink, Warning, TEXT("Shipping 配置不带 MCP 服务器（编译期剔除）"));
#else
	const bool bRunning = McpServer.IsValid() && McpServer->IsRunning();
	auto PrintListen = [this, &Ar](const TCHAR* Prefix)
	{
		const int32 McpPort = McpServer.IsValid() ? McpServer->GetMcpPort() : 0;
		const int32 WsPort  = McpServer.IsValid() ? McpServer->GetWsPort() : 0;
		Ar.Logf(TEXT("%s http://127.0.0.1:%d/stream  ws://127.0.0.1:%d/"), Prefix, McpPort, WsPort);
	};

	if (Args.Num() < 1)
	{
		if (bRunning)
		{
			PrintListen(TEXT("NexusLink.EnableMcp 当前=on"));
		}
		else
		{
			Ar.Log(TEXT("NexusLink.EnableMcp 当前=off（用法: NexusLink.EnableMcp 1|0 [Port=] [WsPort=] [Lan=1|-NexusAllowLan]）"));
		}
		return;
	}

	bool bEnable = false;
	bool bDisable = false;
	const bool bHasOnOff = ParseEnableToken(Args[0], bEnable, bDisable);
	if (!bHasOnOff)
	{
		Ar.Logf(TEXT("NexusLink.EnableMcp 参数无效 '%s'（期望 1|0）"), *Args[0]);
		UE_LOG(LogNexusLink, Warning, TEXT("NexusLink.EnableMcp 参数无效 '%s'（期望 1|0）"), *Args[0]);
		return;
	}

	int8 LanOverride = -1;
	const bool bGotOverride = ApplyListenOverridesFromArgs(
		Args, 1, SessionMcpPort, SessionWsPort, LanOverride);
	if (LanOverride >= 0)
	{
		FNexusMcpServer::SetSessionLanBindOverride(LanOverride);
	}

	if (bDisable)
	{
		SessionMcpPort = 0;
		SessionWsPort = 0;
		FNexusMcpServer::SetSessionLanBindOverride(-1);
		StopMcpServer();
		Ar.Log(TEXT("NexusLink.EnableMcp: MCP 已关闭（会话级，未写 Preferences）"));
		UE_LOG(LogNexusLink, Log, TEXT("NexusLink.EnableMcp: MCP 已关闭（会话级，未写 Preferences）"));
		return;
	}

	if (bRunning && bGotOverride)
	{
		StopMcpServer();
	}
	if (TryStartMcpServer())
	{
		PrintListen(TEXT("NexusLink.EnableMcp: MCP 已开启（会话级，未写 Preferences）"));
		UE_LOG(LogNexusLink, Log, TEXT("NexusLink.EnableMcp: MCP 已开启（会话级，未写 Preferences）"));
	}
	else
	{
		Ar.Log(TEXT("NexusLink.EnableMcp: 启动失败"));
		UE_LOG(LogNexusLink, Error, TEXT("NexusLink.EnableMcp: 启动失败"));
	}
#endif
}

IMPLEMENT_MODULE(FNexusLinkModule, NexusLink)
