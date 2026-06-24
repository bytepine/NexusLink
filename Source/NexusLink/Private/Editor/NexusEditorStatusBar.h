// Copyright byteyang. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

class FExtender;

/**
 * NexusLink 编辑器状态栏工具栏组件。
 * 在 Level Editor 工具栏末尾显示当前实际运行的 MCP/WS 端口，
 * 点击可打开 NexusLink 设置面板。
 */
struct FNexusEditorStatusBar
{
	/** 在 Level Editor 工具栏注册状态组件。返回 Extender，关闭模块时须传回 Unregister。 */
	static TSharedPtr<FExtender> Register(int32 McpPort, int32 WsPort);

	/** 从 Level Editor 工具栏移除状态组件。 */
	static void Unregister(TSharedPtr<FExtender> Extender);

	/**
	 * 向 UEngine 注册统计绘制项，并为所有已打开的编辑器视口自动启用，
	 * 使端口信息显示在视口右侧覆盖层（与"显示帧率和内存"同区域）。
	 */
	static void RegisterViewportStats(int32 McpPort, int32 WsPort);

	/** 从所有编辑器视口的已启用统计列表中移除 NexusLink 统计项。 */
	static void UnregisterViewportStats();
};

#endif // WITH_EDITOR
