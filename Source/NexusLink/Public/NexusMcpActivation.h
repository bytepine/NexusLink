// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** MCP 启停请求的生效来源，供日志/状态命令展示。 */
enum class ENexusMcpSource : uint8
{
	/** 未请求启动。 */
	None,
	/** 编辑器 Preferences 勾选（仅可信角色生效）。 */
	Preferences,
	/** 启动参数 -EnableNexusMcp。 */
	CommandLine,
	/** 运行时控制台 NexusLink.Mcp。 */
	Console,
};

/** 监听端口与 LAN 绑定的最终解析结果。 */
struct FNexusMcpListenConfig
{
	int32 McpPort = 45000;
	int32 WsPort  = 55000;
	bool  bLan    = false;
};

/**
 * MCP 启停 / 监听配置的单一真值来源。
 *
 * 优先级统一为：**控制台会话覆盖 > 启动参数 > Preferences（仅可信角色）> 默认值**。
 * 「可信角色」= 编辑器进程且非 commandlet（`GIsEditor && !IsRunningCommandlet()`）；
 * 编辑器二进制以 `-game` / `-server` 启动的子进程、cook/commandlet 进程不读 Preferences，
 * 必须显式给 `-EnableNexusMcp` 或运行时控制台才会启动，避免继承勾选各自抢起一份 MCP。
 *
 * 控制台覆盖为进程内会话级状态（不写 Preferences、不落盘），三态设计使 `off` 能稳定压制
 * 后续任意 Preferences 改动（例如勾选局域网绑定）重新拉起服务。
 */
struct NEXUSLINK_API FNexusMcpActivation
{
	FNexusMcpActivation() = delete;

	/**
	 * 控制台会话级启停覆盖；未设置时不参与判定。
	 * 设为 false 会稳定压制后续任意 Preferences 改动重新拉起服务，直到再次显式
	 * SetConsoleEnable(true) 或 Preferences 的 bEnableMcpServer 本身被用户切换。
	 */
	static void SetConsoleEnable(TOptional<bool> InEnable);

	/** 控制台会话级监听覆盖。McpPort/WsPort<=0 表示不覆盖；Lan：-1=不覆盖，0=强制关闭，1=强制开启。 */
	static void SetConsoleListenOverride(int32 InMcpPort, int32 InWsPort, int8 InLan);

	/**
	 * 清空控制台会话级监听覆盖（Port/WsPort/Lan），回到「启动参数 / Preferences / 默认值」判定。
	 * 不影响 SetConsoleEnable 的启停覆盖——关闭 MCP 时监听配置回默认，但「关闭」这个决定本身
	 * 仍需保留（见 SetConsoleEnable 说明），否则 status 诊断会与实际运行状态矛盾。
	 */
	static void ClearConsoleOverrides();

	/** 当前控制台启停覆盖（供 status 命令展示）；未设置返回 unset。 */
	static TOptional<bool> GetConsoleEnable();

	/** 当前进程是否信任 Preferences 勾选（编辑器进程且非 commandlet）。 */
	static bool IsPreferencesTrusted();

	/**
	 * 解析当前是否应启动 MCP。
	 * @param OutSource 命中的生效来源；未命中时为 ENexusMcpSource::None。
	 * @param OutReason 人类可读的原因（命中时说明来源，未命中时说明为何未启动）。
	 */
	static bool IsRequested(ENexusMcpSource& OutSource, FString& OutReason);

	/** 解析监听端口与 LAN 绑定，遵循与 IsRequested 相同的优先级。 */
	static FNexusMcpListenConfig ResolveListenConfig();
};
