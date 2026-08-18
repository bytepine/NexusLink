// Copyright byteyang. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

/**
 * NexusLink 编辑器端口指示。
 * 通过 Level Editor 标题栏条目挂到与 FPS/内存/对象同一组（窗口右上）。
 */
struct FNexusEditorStatusBar
{
	/** 向 Level Editor 标题栏注册 MCP/WS 端口条目。 */
	static void Register(int32 McpPort, int32 WsPort);

	/** 从 Level Editor 标题栏移除端口条目。 */
	static void Unregister();
};

#endif // WITH_EDITOR
