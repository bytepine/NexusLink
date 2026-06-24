// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FNexusMcpServer;
class FNexusLogCapture;
class FExtender;

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

	/** 按设置尝试启动 MCP 服务器（已在运行则跳过）。 */
	bool TryStartMcpServer();

	/** 停止 MCP 服务器并清理实例注册与状态栏。 */
	void StopMcpServer();

private:
	/** 延迟到引擎完全初始化后再按设置启动 MCP 服务器。 */
	void OnPostEngineInit();

	TSharedPtr<FNexusMcpServer>  McpServer;
	/** 日志捕获器，模块生命周期内持续收集 UE 输出日志。 */
	TUniquePtr<FNexusLogCapture> LogCapture;

#if WITH_EDITOR
	/** Level Editor 工具栏状态组件的 Extender 句柄，关闭时用于移除。 */
	TSharedPtr<FExtender> ToolbarExtender;
#endif
};

