// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusLinkBuildConfig.h"

// Shipping 不带 MCP 服务器（NEXUSLINK_WITH_SERVER=0），调试面板同样裁掉，不进 Shipping 二进制。
#if NEXUSLINK_WITH_SERVER

class SWidget;
class UWorld;
class UGameViewportClient;
class APlayerController;

/**
 * 游戏内 MCP 调试面板挂载器（PIE / 独立包通用）。
 * 走 GameViewport 叠加层（AddViewportWidgetContent），不依赖任何 UMG 资产；
 * 同一时刻至多一份面板，由 NexusLink.Mcp panel 控制台命令驱动开关。
 */
struct FNexusMcpDebugOverlay
{
	FNexusMcpDebugOverlay() = delete;

	/** 当前是否已打开。 */
	static bool IsOpen();

	/** 打开面板（已打开则无操作）；World 用于定位 GameViewport / PlayerController。 */
	static void Open(UWorld* World);

	/** 关闭面板并尽量还原打开前的鼠标显示 / 输入模式。 */
	static void Close();

	/** 已打开则关闭，否则打开。 */
	static void Toggle(UWorld* World);

private:
	static void RestoreInputState();

	static TWeakPtr<SWidget>                        PanelWidget;
	static TWeakObjectPtr<UGameViewportClient>       OwningViewport;
	static TWeakObjectPtr<APlayerController>         OwningPC;
	/**
	 * 打开前的鼠标显示状态，关闭时按此近似还原输入模式（无法反查当时精确的 FInputModeType，
	 * 只能在 GameAndUI / GameOnly 之间按这个布尔近似）。
	 */
	static bool bPrevShowMouseCursor;
};

#endif // NEXUSLINK_WITH_SERVER
