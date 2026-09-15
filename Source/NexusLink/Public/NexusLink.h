// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FNexusMcpServer;
class FNexusLogCapture;

/**
 * NexusLink 模块 —— UE 端 MCP 服务器入口。
 * 在 StartupModule 中启动 TCP MCP 服务器，
 * 在 ShutdownModule 中优雅关闭。
 */
class FNexusLinkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 获取当前 MCP 服务器实例（供设置面板等内部模块查询当前端口）。 */
	const TSharedPtr<FNexusMcpServer>& GetMcpServer() const { return McpServer; }

	/** 尝试启动 MCP 服务器（已在运行则跳过）。不读 Preferences；由调用方决定是否启动。 */
	bool TryStartMcpServer();

	/** 停止 MCP 服务器并清理实例注册与状态栏。 */
	void StopMcpServer();

	/**
	 * 按 FNexusMcpActivation::IsRequested() 的解析结果启停 MCP。
	 * 唯一的「期望状态」应用入口：启动时、Preferences 勾选变化、控制台命令均调此函数，
	 * 避免三条路径各自直调 TryStart/Stop 导致状态不一致。
	 * @param Ar 可选：非空时把「未启动原因」也输出到此处（供控制台命令回显）。
	 */
	void ApplyDesiredMcpState(class FOutputDevice* Ar = nullptr);

private:
	/** 延迟到引擎完全初始化后再按设置启动 MCP 服务器。 */
	void OnPostEngineInit();

	/** 控制台：NexusLink.Mcp on|off|status|restart（会话级，不写 Preferences）。回显走 FOutputDevice，独立 Game 包 `~` 可见。 */
	void HandleMcpCommand(const TArray<FString>& Args, class UWorld* World, class FOutputDevice& Ar);

	TSharedPtr<FNexusMcpServer>  McpServer;
	/** 日志捕获器，模块生命周期内持续收集 UE 输出日志。 */
	TUniquePtr<FNexusLogCapture> LogCapture;

	/** NexusLink.Mcp 控制台命令句柄。 */
	class IConsoleObject* McpConsoleCommand = nullptr;
};

