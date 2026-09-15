// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Editor（私有头，仅 NexusCaptureEditorPanelCapability 使用）
#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Widgets/Docking/SDockTab.h"
#endif

class AActor;

/**
 * 编辑器截图辅助：LevelEditor 面板 tab 查找、编辑器视口 SceneView 投影、
 * 相机视角环绕拍摄。像素裁切 / 缩放 / 保存等通用部分复用 FNexusCaptureUtils（Runtime 模块）。
 */
class FNexusEditorCaptureUtils final
{
public:
	FNexusEditorCaptureUtils() = delete;

	/** 面板名称 → Tab ID 映射（延迟初始化单例）。 */
	static const TMap<FString, FString>& GetPanelTabMapping();

#if WITH_EDITOR
	/** 按面板名或 Tab ID 查找 DockTab；未找到返回无效指针。 */
	static TSharedPtr<SDockTab> FindPanelTab(const FString& Name);

	/** 取面板可截图的 Slate widget：优先整个 SDockingTabStack，退回 tab 内容。 */
	static TSharedPtr<SWidget> GetPanelCaptureWidget(const TSharedRef<SDockTab>& Tab);

	/** 按 Actor 朝向和视角名称（front/back/left/right/top/bottom）计算摄像机方向向量。 */
	static FVector GetViewDirection(AActor* Actor, const FString& ViewAngle);

	/** 用编辑器透视视口的 SceneView 投影 Actor 包围盒到屏幕矩形（Padding 为留白比例）。 */
	static bool GetActorScreenRectInEditorViewport(AActor* Actor, float Padding,
	                                               FIntRect& OutRect, int32& OutViewW, int32& OutViewH);

	/**
	 * 移动编辑器相机到指定角度后截取视口像素并裁切到 Actor 包围盒。
	 * 完成后恢复相机位置。
	 */
	static bool CaptureActorFromAngle(AActor* Actor, const FString& ViewAngle,
	                                  float PaddingRatio,
	                                  TArray<FColor>& OutPixels, int32& OutW, int32& OutH);
#endif
};
