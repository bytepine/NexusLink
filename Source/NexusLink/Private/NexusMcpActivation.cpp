// Copyright byteyang. All Rights Reserved.

#include "NexusMcpActivation.h"
#include "NexusLinkSettings.h"
#include "CoreGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// 进程内会话级状态，不带 Config，任何 SaveConfig 都不会把它落进 ini。CapabilitySpec §8.3 禁止匿名命名空间，
// 用文件级 static 替代（NexusMcpActivation.cpp 独占 TU，符号不会泄漏到其他文件）。
static TOptional<bool> GConsoleEnableOverride;
static int32 GConsoleMcpPort = 0;  // <=0 = 不覆盖
static int32 GConsoleWsPort  = 0;  // <=0 = 不覆盖
static int8  GConsoleLan     = -1; // -1=不覆盖，0=强制关闭，1=强制开启

void FNexusMcpActivation::SetConsoleEnable(TOptional<bool> InEnable)
{
	GConsoleEnableOverride = InEnable;
}

void FNexusMcpActivation::SetConsoleListenOverride(int32 InMcpPort, int32 InWsPort, int8 InLan)
{
	if (InMcpPort > 0)
	{
		GConsoleMcpPort = InMcpPort;
	}
	if (InWsPort > 0)
	{
		GConsoleWsPort = InWsPort;
	}
	if (InLan >= 0)
	{
		GConsoleLan = InLan;
	}
}

void FNexusMcpActivation::ClearConsoleOverrides()
{
	GConsoleEnableOverride.Reset();
	GConsoleMcpPort = 0;
	GConsoleWsPort  = 0;
	GConsoleLan     = -1;
}

TOptional<bool> FNexusMcpActivation::GetConsoleEnable()
{
	return GConsoleEnableOverride;
}

bool FNexusMcpActivation::IsPreferencesTrusted()
{
	// 编辑器二进制以 -game / -server 启动的子进程、cook/commandlet 进程虽链接同一份
	// EditorPerProjectUserSettings，但不应继承勾选各自抢起一份 MCP，须显式给启动参数或控制台。
	return GIsEditor && !IsRunningCommandlet();
}

bool FNexusMcpActivation::IsRequested(ENexusMcpSource& OutSource, FString& OutReason)
{
	OutSource = ENexusMcpSource::None;

	if (GConsoleEnableOverride.IsSet())
	{
		if (GConsoleEnableOverride.GetValue())
		{
			OutSource = ENexusMcpSource::Console;
			OutReason = TEXT("控制台会话已开启");
			return true;
		}
		OutReason = TEXT("已被控制台会话关闭");
		return false;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("EnableNexusMcp")))
	{
		OutSource = ENexusMcpSource::CommandLine;
		OutReason = TEXT("启动参数 -EnableNexusMcp");
		return true;
	}

	if (!IsPreferencesTrusted())
	{
		OutReason = TEXT("当前角色不读 Preferences（非编辑器角色需显式 -EnableNexusMcp 或运行时控制台开启）");
		return false;
	}

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (Settings && Settings->bEnableMcpServer)
	{
		OutSource = ENexusMcpSource::Preferences;
		OutReason = TEXT("Preferences 勾选「启用 MCP 服务器」");
		return true;
	}

	OutReason = TEXT("未勾选 Preferences 且无启动参数 -EnableNexusMcp");
	return false;
}

FNexusMcpListenConfig FNexusMcpActivation::ResolveListenConfig()
{
	FNexusMcpListenConfig Config;

	// 端口：控制台 > 启动参数 > 默认；Preferences 只读回显，不作为端口输入源。
	if (GConsoleMcpPort > 0)
	{
		Config.McpPort = GConsoleMcpPort;
	}
	else
	{
		int32 CliPort = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("NexusMcpPort="), CliPort) && CliPort > 0 && CliPort <= 65535)
		{
			Config.McpPort = CliPort;
		}
	}

	if (GConsoleWsPort > 0)
	{
		Config.WsPort = GConsoleWsPort;
	}
	else
	{
		int32 CliWsPort = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("NexusWsPort="), CliWsPort) && CliWsPort > 0 && CliWsPort <= 65535)
		{
			Config.WsPort = CliWsPort;
		}
	}

	// LAN：控制台 > 启动参数 -NexusAllowLan > Preferences（仅可信角色）> 默认关闭。
	if (GConsoleLan >= 0)
	{
		Config.bLan = GConsoleLan != 0;
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("NexusAllowLan")))
	{
		Config.bLan = true;
	}
	else if (IsPreferencesTrusted() && UNexusLinkSettings::Get() && UNexusLinkSettings::Get()->bAllowLanBind)
	{
		Config.bLan = true;
	}
	else
	{
		Config.bLan = false;
	}

	return Config;
}
