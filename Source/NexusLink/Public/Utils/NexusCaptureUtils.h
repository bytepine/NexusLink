// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Widgets/SWindow.h"

class AActor;
class UWidget;

/**
 * Runtime 截图辅助：Game/PIE 视口 ReadPixels、顶层窗口 FWidgetRenderer、
 * Actor/UMG 屏幕包围盒裁切、PNG/JPEG 保存。不依赖 UnrealEd / LevelEditor。
 *
 * 导出为 Public：NexusLinkEditor 的 capture_editor_panel 复用这里的像素裁切 /
 * 缩放 / 保存与 Slate 渲染，编辑器侧只补 LevelEditor 面板 tab 与相机视角部分。
 */
class NEXUSLINK_API FNexusCaptureUtils final
{
public:
	FNexusCaptureUtils() = delete;

	struct FWindowInfo
	{
		TSharedPtr<SWindow> Window;
		FString Title;
		FVector2D Size;
	};

	/** 可见顶层窗口（按面积降序）。Slate 未初始化时返回空。 */
	static TArray<FWindowInfo> GetSortedTopLevelWindows();

	/** GameViewport 像素（FViewport::ReadPixels）。成功返回 true。 */
	static bool CaptureGameViewportPixels(TArray<FColor>& OutPixels, int32& OutW, int32& OutH);

	/** 用 FWidgetRenderer 把 Slate Widget 渲成像素。Dedicated Server 恒为 false。 */
	static bool CaptureWidgetPixels(TSharedRef<SWidget> Widget,
	                                TArray<FColor>& OutPixels, int32& OutW, int32& OutH);

	static bool CropPixels(const TArray<FColor>& Src, int32 SrcW, int32 SrcH,
	                       const FIntRect& Rect, TArray<FColor>& Out, int32& OutW, int32& OutH);

	static bool CropToScreenRect(TArray<FColor>& Pixels, int32& W, int32& H,
	                             const FIntRect& ScreenRect, int32 ViewW, int32 ViewH);

	static void GetActorVisualBounds(AActor* Actor, FVector& OutOrigin, FVector& OutExtent);

	/** 用 PlayerController 投影 Actor 包围盒到当前 Game 视口。 */
	static bool GetActorScreenRect(AActor* Actor, float Padding,
	                               FIntRect& OutRect, int32& OutViewW, int32& OutViewH);

	static bool GetUMGWidgetScreenRect(UWidget* Widget,
	                                   FIntRect& OutRect, int32& OutViewW, int32& OutViewH);

	static void CleanupOldCaptures(const FString& OutputDir, int32 MaxKeep = 20);

	static bool SavePixels(const TArray<FColor>& Pixels, int32 W, int32 H,
	                       const FString& Format, const FString& FilePath);

	static bool SaveAndBuildEntry(TArray<FColor>& Pixels, int32 W, int32 H,
	                              int32 MaxSize, const FString& Format, const FString& FileSuffix,
	                              const TSharedPtr<FJsonObject>& ExtraFields,
	                              TSharedPtr<FJsonObject>& OutEntry, FString& OutError);
};
