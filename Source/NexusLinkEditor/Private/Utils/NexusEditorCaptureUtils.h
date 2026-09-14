// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Editor（私有头，仅 NexusCaptureViewportCapability 使用）
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

#if WITH_EDITOR
#include "Widgets/Docking/SDockTab.h"
#endif

class AActor;
class UWidget;

/**
 * 编辑器视口截图辅助工具：像素裁切、Actor 屏幕包围盒、窗口枚举、保存压缩。
 * 将 NexusCaptureViewportCapability.cpp 的 Cap* 静态函数群集中管理，便于复用与测试。
 */
class FNexusEditorCaptureUtils final
{
public:
	FNexusEditorCaptureUtils() = delete;

	/** 面板名称 → Tab ID 映射（延迟初始化单例）。 */
	static const TMap<FString, FString>& GetPanelTabMapping();

	/** 可见顶层窗口信息（按面积降序）。 */
	struct FWindowInfo
	{
		TSharedPtr<SWindow> Window;
		FString Title;
		FVector2D Size;
	};

	/** 收集所有可见顶层窗口并按面积降序排序。 */
	static TArray<FWindowInfo> GetSortedTopLevelWindows();

#if WITH_EDITOR
	/** 按面板名或 Tab ID 查找 DockTab；未找到返回无效指针。 */
	static TSharedPtr<SDockTab> FindPanelTab(const FString& Name);
#endif

	/** 使用 FWidgetRenderer 将 Slate Widget 渲染为像素数组。成功返回 true。 */
	static bool CaptureWidgetPixels(TSharedRef<SWidget> Widget,
	                                TArray<FColor>& OutPixels, int32& OutW, int32& OutH);

	/** 从像素数组裁切指定矩形区域。成功返回 true。 */
	static bool CropPixels(const TArray<FColor>& Src, int32 SrcW, int32 SrcH,
	                       const FIntRect& Rect, TArray<FColor>& Out, int32& OutW, int32& OutH);

	/** 按屏幕坐标矩形裁切像素（自动换算像素坐标）。成功返回 true。 */
	static bool CropToScreenRect(TArray<FColor>& Pixels, int32& W, int32& H,
	                             const FIntRect& ScreenRect, int32 ViewW, int32 ViewH);

	/** 计算 Actor 的可视包围盒（优先使用 MeshComponent bounds）。 */
	static void GetActorVisualBounds(AActor* Actor, FVector& OutOrigin, FVector& OutExtent);

	/** 由 Actor 朝向和视角名称（front/back/left/right/top/bottom）计算摄像机方向向量。 */
	static FVector GetViewDirection(AActor* Actor, const FString& ViewAngle);

	/**
	 * 计算 Actor 包围盒在当前视口的屏幕空间矩形（含 Padding 比例留白）。
	 * bIsPIE: 使用 PIE PlayerController 投影；否则使用编辑器视口 SceneView。
	 */
	static bool GetActorScreenRect(AActor* Actor, bool bIsPIE, float Padding,
	                               FIntRect& OutRect, int32& OutViewW, int32& OutViewH);

	/** 计算 UMG Widget 在 PIE 视口坐标系中的屏幕矩形。 */
	static bool GetUMGWidgetScreenRect(UWidget* Widget,
	                                   FIntRect& OutRect, int32& OutViewW, int32& OutViewH);

#if WITH_EDITOR
	/**
	 * 移动编辑器相机到指定角度后截取视口像素并裁切到 Actor 包围盒。
	 * 完成后恢复相机位置。
	 */
	static bool CaptureActorFromAngle(AActor* Actor, const FString& ViewAngle,
	                                  float PaddingRatio,
	                                  TArray<FColor>& OutPixels, int32& OutW, int32& OutH);
#endif

	/** 删除输出目录中超出 MaxKeep 的旧截图文件（按文件名排序）。 */
	static void CleanupOldCaptures(const FString& OutputDir, int32 MaxKeep = 20);

	/** 将像素编码为 PNG/JPEG 并写入文件。成功返回 true。 */
	static bool SavePixels(const TArray<FColor>& Pixels, int32 W, int32 H,
	                       const FString& Format, const FString& FilePath);

	/**
	 * 缩放像素到 MaxSize、保存到 NexusCaptures 目录，并填充 OutEntry JSON。
	 * ExtraFields 中的字段会合并写入 OutEntry。
	 * 失败时 OutError 非空，返回 false。
	 */
	static bool SaveAndBuildEntry(TArray<FColor>& Pixels, int32 W, int32 H,
	                              int32 MaxSize, const FString& Format, const FString& FileSuffix,
	                              const TSharedPtr<FJsonObject>& ExtraFields,
	                              TSharedPtr<FJsonObject>& OutEntry, FString& OutError);
};
