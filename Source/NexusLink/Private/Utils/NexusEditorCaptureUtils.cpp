// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorCaptureUtils.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "Components/Widget.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "SceneView.h"
#endif

#if WITH_EDITOR

// ── 内部静态辅助（仅本文件可见）──────────────────────────────────────────────
// ── 面板名称 → Tab ID 映射 ────────────────────────────────────────────

static const TMap<FString, FString>& GetCapPanelTabMapping()
{
	static TMap<FString, FString> M;
	if (M.Num() == 0)
	{
		M.Add(TEXT("viewport"),        TEXT("LevelEditorViewport"));
		M.Add(TEXT("content_browser"), TEXT("ContentBrowserTab1"));
		M.Add(TEXT("scene_outliner"),  TEXT("LevelEditorSceneOutliner"));
		M.Add(TEXT("details"),         TEXT("LevelEditorSelectionDetails"));
		M.Add(TEXT("output_log"),      TEXT("OutputLog"));
		M.Add(TEXT("modes"),           TEXT("LevelEditorToolBox"));
		M.Add(TEXT("world_settings"),  TEXT("WorldSettingsTab"));
	}
	return M;
}

// ── 收集所有可见顶层窗口（按面积降序）─────────────────────────────

struct FCapWindowInfo
{
	TSharedPtr<SWindow> Window;
	FString Title;
	FVector2D Size;
};

static TArray<FCapWindowInfo> GetCapSortedTopLevelWindows()
{
	TArray<FCapWindowInfo> Result;
	TArray<TSharedRef<SWindow>> TopWindows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(TopWindows);
	for (const TSharedRef<SWindow>& Win : TopWindows)
	{
		if (Win->IsRegularWindow())
		{
			FCapWindowInfo Info;
			Info.Window = Win;
			Info.Title  = Win->GetTitle().ToString();
			Info.Size   = Win->GetSizeInScreen();
			Result.Add(Info);
		}
	}
	Result.Sort([](const FCapWindowInfo& A, const FCapWindowInfo& B) {
		return (A.Size.X * A.Size.Y) > (B.Size.X * B.Size.Y);
	});
	return Result;
}

// ── FWidgetRenderer 截图 ──────────────────────────────────────────────

static bool CapCaptureWidgetPixels(TSharedRef<SWidget> Widget,
	TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	const FGeometry Geo = Widget->GetCachedGeometry();
	FVector2D SlateSize = Geo.GetAbsoluteSize();
	if (SlateSize.X <= 0 || SlateSize.Y <= 0) return false;

	const float DPIScale = FSlateApplication::Get().GetApplicationScale();
	OutW = FMath::Max(1, FMath::RoundToInt(SlateSize.X * DPIScale));
	OutH = FMath::Max(1, FMath::RoundToInt(SlateSize.Y * DPIScale));

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RT->InitCustomFormat(OutW, OutH, PF_B8G8R8A8, /*bInForceLinearGamma=*/true);
	RT->UpdateResourceImmediate(true);

	FWidgetRenderer Renderer(/*bUseGammaCorrection=*/true, /*bClearTarget=*/true);
	Renderer.DrawWidget(RT, Widget, SlateSize, 0.f);
	FlushRenderingCommands();

	FTextureRenderTargetResource* RTRes = RT->GameThread_GetRenderTargetResource();
	if (!RTRes)
	{
		RT->ConditionalBeginDestroy();
		return false;
	}

	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	bool bOK = RTRes->ReadPixels(OutPixels, ReadFlags);
	RT->ConditionalBeginDestroy();

	return bOK && OutPixels.Num() == OutW * OutH;
}

// ── 像素裁切 ─────────────────────────────────────────────────────────

static bool CapCropPixels(const TArray<FColor>& Src, int32 SrcW, int32 SrcH,
	const FIntRect& Rect, TArray<FColor>& Out, int32& OutW, int32& OutH)
{
	OutW = Rect.Width();
	OutH = Rect.Height();
	if (OutW <= 0 || OutH <= 0) return false;

	Out.SetNumUninitialized(OutW * OutH);
	for (int32 Y = 0; Y < OutH; ++Y)
	{
		const int32 SrcY = Rect.Min.Y + Y;
		if (SrcY < 0 || SrcY >= SrcH) continue;
		const FColor* SrcRow = &Src[SrcY * SrcW];
		FColor* DstRow = &Out[Y * OutW];
		for (int32 X = 0; X < OutW; ++X)
		{
			const int32 SrcX = Rect.Min.X + X;
			if (SrcX >= 0 && SrcX < SrcW)
				DstRow[X] = SrcRow[SrcX];
		}
	}
	return true;
}

static bool CapCropToScreenRect(TArray<FColor>& Pixels, int32& W, int32& H,
	const FIntRect& ScreenRect, int32 ViewW, int32 ViewH)
{
	float RatioX = (float)W / (float)FMath::Max(1, ViewW);
	float RatioY = (float)H / (float)FMath::Max(1, ViewH);
	FIntRect PixelRect;
	PixelRect.Min.X = FMath::Clamp(FMath::FloorToInt(ScreenRect.Min.X * RatioX), 0, W);
	PixelRect.Min.Y = FMath::Clamp(FMath::FloorToInt(ScreenRect.Min.Y * RatioY), 0, H);
	PixelRect.Max.X = FMath::Clamp(FMath::CeilToInt(ScreenRect.Max.X * RatioX),  0, W);
	PixelRect.Max.Y = FMath::Clamp(FMath::CeilToInt(ScreenRect.Max.Y * RatioY),  0, H);

	TArray<FColor> Cropped;
	int32 CW = 0, CH = 0;
	if (CapCropPixels(Pixels, W, H, PixelRect, Cropped, CW, CH))
	{
		Pixels = MoveTemp(Cropped);
		W = CW;
		H = CH;
		return true;
	}
	return false;
}

// ── 面板 Tab 查找 ────────────────────────────────────────────────────

#if WITH_EDITOR

static TSharedPtr<SDockTab> CapFindPanelTab(const FString& Name)
{
	const TMap<FString, FString>& Map = GetCapPanelTabMapping();
	const FString* TabId = Map.Find(Name);
	const FString Actual = TabId ? *TabId : Name;

	if (FLevelEditorModule* LE = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		TSharedPtr<FTabManager> TM = LE->GetLevelEditorTabManager();
		if (TM.IsValid())
		{
			TSharedPtr<SDockTab> Tab = TM->FindExistingLiveTab(FTabId(FName(*Actual)));
			if (Tab.IsValid()) return Tab;
		}
	}
	return FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(*Actual)));
}

#endif

// ── Actor 可视包围盒 ─────────────────────────────────────────────────

static void CapGetActorVisualBounds(AActor* Actor, FVector& OutOrigin, FVector& OutExtent)
{
	bool bFoundMesh = false;
	TArray<UPrimitiveComponent*> PrimComps;
	Actor->GetComponents<UPrimitiveComponent>(PrimComps);
	FBox VisualBox(ForceInit);
	for (UPrimitiveComponent* PC : PrimComps)
	{
		if (!PC || !PC->IsVisible()) continue;
		if (PC->IsA(UMeshComponent::StaticClass()))
		{
			VisualBox += PC->Bounds.GetBox();
			bFoundMesh = true;
		}
	}
	if (bFoundMesh)
	{
		OutOrigin = VisualBox.GetCenter();
		OutExtent = VisualBox.GetExtent();
	}
	else
	{
		Actor->GetActorBounds(true, OutOrigin, OutExtent);
		if (OutExtent.IsNearlyZero())
		{
			OutOrigin = Actor->GetActorLocation();
			OutExtent = FVector(50.f);
		}
	}
}

// ── Actor 屏幕包围盒（前向声明）────────────────────────────────────

static bool CapGetActorScreenRect(AActor* Actor, bool bIsPIE, float Padding, FIntRect& OutRect,
	int32& OutViewW, int32& OutViewH);

// ── 视角方向辅助 ──────────────────────────────────────────────────

static FVector CapGetViewDirection(AActor* Actor, const FString& ViewAngle)
{
	FRotator ActorRot = Actor->GetActorRotation();
	FVector Forward = FRotationMatrix(ActorRot).GetUnitAxis(EAxis::X);
	FVector Right   = FRotationMatrix(ActorRot).GetUnitAxis(EAxis::Y);

	if (ViewAngle == TEXT("back"))   return -Forward;
	if (ViewAngle == TEXT("left"))   return -Right;
	if (ViewAngle == TEXT("right"))  return  Right;
	if (ViewAngle == TEXT("top"))    return  FVector::UpVector;
	if (ViewAngle == TEXT("bottom")) return -FVector::UpVector;
	return Forward;
}

// ── PIE 弹出 RAII 守卫 ───────────────────────────────────────────────

#if WITH_EDITOR
struct FCapScopedPIEEject
{
	TSharedPtr<IAssetViewport> Viewport;
	bool bDidEject = false;

	FCapScopedPIEEject()
	{
		if (!GEditor || !GEditor->IsPlayingSessionInEditor() || GEditor->bIsSimulatingInEditor)
			return;
		if (FLevelEditorModule* LE = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
			Viewport = LE->GetFirstActiveViewport();
		if (!Viewport.IsValid()) return;

		Viewport->SwapViewportsForSimulateInEditor();
		Viewport->GetAssetViewportClient().SetIsSimulateInEditorViewport(true);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->SetIsSimulateInEditorViewport(true);
			GEngine->GameViewport->GetGameViewport()->SetPlayInEditorIsSimulate(true);
		}
		GEditor->bIsSimulatingInEditor = true;
		bDidEject = true;
	}

	~FCapScopedPIEEject()
	{
		if (!bDidEject || !Viewport.IsValid()) return;
		if (GEngine && GEngine->GameViewport)
			GEngine->GameViewport->GetGameViewport()->SetPlayInEditorIsSimulate(false);
		Viewport->SwapViewportsForPlayInEditor();
		if (GEngine && GEngine->GameViewport)
			GEngine->GameViewport->SetIsSimulateInEditorViewport(false);
		Viewport->GetAssetViewportClient().SetIsSimulateInEditorViewport(false);
		GEditor->bIsSimulatingInEditor = false;
	}
};

static bool CapCaptureActorFromAngle(AActor* Actor, const FString& ViewAngle,
	float PaddingRatio, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	if (!Actor || !GEditor) return false;

	FVector Origin, Extent;
	CapGetActorVisualBounds(Actor, Origin, Extent);

	const float BoundsRadius = Extent.Size();
	const float FOV = 90.f;
	const float HalfFOVRad = FMath::DegreesToRadians(FOV * 0.5f);
	const float Distance = FMath::Max(200.f, (BoundsRadius / FMath::Tan(HalfFOVRad)) * (2.0f + PaddingRatio));

	FVector Direction = CapGetViewDirection(Actor, ViewAngle);
	FVector CamPos = Origin + Direction * Distance;
	FRotator CamRot = (Origin - CamPos).Rotation();

	FCapScopedPIEEject PIEGuard;

	FLevelEditorViewportClient* Client = nullptr;
	for (FLevelEditorViewportClient* VC : GEditor->GetLevelViewportClients())
	{
		if (VC && VC->IsPerspective()) { Client = VC; break; }
	}
	if (!Client) return false;

	FVector SavedLoc = Client->GetViewLocation();
	FRotator SavedRot = Client->GetViewRotation();

	Client->SetViewLocation(CamPos);
	Client->SetViewRotation(CamRot);
	if (Client->Viewport)
	{
		Client->Viewport->Draw(false);
		FlushRenderingCommands();
	}

	bool bOk = false;
	TSharedPtr<SDockTab> VPTab = CapFindPanelTab(TEXT("viewport"));
	if (VPTab.IsValid())
	{
		TSharedPtr<SWidget> VPContent = VPTab->GetContent();
		if (VPContent.IsValid())
			bOk = CapCaptureWidgetPixels(VPContent.ToSharedRef(), OutPixels, OutW, OutH);
	}

	if (bOk)
	{
		FIntRect ActorRect;
		int32 ViewW = 0, ViewH = 0;
		if (CapGetActorScreenRect(Actor, false, PaddingRatio, ActorRect, ViewW, ViewH))
			CapCropToScreenRect(OutPixels, OutW, OutH, ActorRect, ViewW, ViewH);
	}

	Client->SetViewLocation(SavedLoc);
	Client->SetViewRotation(SavedRot);
	if (Client->Viewport)
	{
		Client->Viewport->Draw(false);
		FlushRenderingCommands();
	}

	return bOk;
}
#endif // WITH_EDITOR

// ── Actor 屏幕包围盒 ─────────────────────────────────────────────────

static bool CapGetActorScreenRect(AActor* Actor, bool bIsPIE, float Padding, FIntRect& OutRect,
	int32& OutViewW, int32& OutViewH)
{
	if (!Actor) return false;

	FVector Origin, Extent;
	CapGetActorVisualBounds(Actor, Origin, Extent);

	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		Corners[i] = Origin + FVector(
			(i & 1) ? Extent.X : -Extent.X,
			(i & 2) ? Extent.Y : -Extent.Y,
			(i & 4) ? Extent.Z : -Extent.Z);
	}

	FVector2D ScreenMin(FLT_MAX, FLT_MAX);
	FVector2D ScreenMax(-FLT_MAX, -FLT_MAX);
	int32 ValidCount = 0;

	if (bIsPIE)
	{
		UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
		if (!World) return false;
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) return false;

		int32 VPW, VPH;
		PC->GetViewportSize(VPW, VPH);
		OutViewW = VPW;
		OutViewH = VPH;

		for (const FVector& Pt : Corners)
		{
			FVector2D SP;
			if (PC->ProjectWorldLocationToScreen(Pt, SP, true))
			{
				ScreenMin.X = FMath::Min(ScreenMin.X, SP.X);
				ScreenMin.Y = FMath::Min(ScreenMin.Y, SP.Y);
				ScreenMax.X = FMath::Max(ScreenMax.X, SP.X);
				ScreenMax.Y = FMath::Max(ScreenMax.Y, SP.Y);
				++ValidCount;
			}
		}
	}
	else
	{
#if WITH_EDITOR
		FLevelEditorViewportClient* Client = nullptr;
		for (FLevelEditorViewportClient* VC : GEditor->GetLevelViewportClients())
		{
			if (VC && VC->IsPerspective() && VC->Viewport)
			{
				Client = VC;
				break;
			}
		}
		if (!Client || !Client->Viewport) return false;

		FIntPoint VPSize = Client->Viewport->GetSizeXY();
		OutViewW = VPSize.X;
		OutViewH = VPSize.Y;

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
		FSceneView* View = Client->CalcSceneView(&ViewFamily);
		if (!View) return false;

		for (const FVector& Pt : Corners)
		{
			FVector2D SP;
			if (View->ScreenToPixel(View->WorldToScreen(Pt), SP))
			{
				ScreenMin.X = FMath::Min(ScreenMin.X, SP.X);
				ScreenMin.Y = FMath::Min(ScreenMin.Y, SP.Y);
				ScreenMax.X = FMath::Max(ScreenMax.X, SP.X);
				ScreenMax.Y = FMath::Max(ScreenMax.Y, SP.Y);
				++ValidCount;
			}
		}
#else
		return false;
#endif
	}

	if (ValidCount == 0) return false;

	float PadX = (ScreenMax.X - ScreenMin.X) * Padding;
	float PadY = (ScreenMax.Y - ScreenMin.Y) * Padding;

	OutRect.Min.X = FMath::Clamp(FMath::FloorToInt(ScreenMin.X - PadX), 0, OutViewW);
	OutRect.Min.Y = FMath::Clamp(FMath::FloorToInt(ScreenMin.Y - PadY), 0, OutViewH);
	OutRect.Max.X = FMath::Clamp(FMath::CeilToInt(ScreenMax.X + PadX),  0, OutViewW);
	OutRect.Max.Y = FMath::Clamp(FMath::CeilToInt(ScreenMax.Y + PadY),  0, OutViewH);

	return OutRect.Width() > 0 && OutRect.Height() > 0;
}

// ── UMG Widget 屏幕区域 ──────────────────────────────────────────────

static bool CapGetUMGWidgetScreenRect(UWidget* Widget, FIntRect& OutRect,
	int32& OutViewW, int32& OutViewH)
{
	if (!Widget) return false;

	TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid()) return false;

	const FGeometry Geo = SlateWidget->GetCachedGeometry();
	const FVector2D AbsPos  = Geo.GetAbsolutePosition();
	const FVector2D AbsSize = Geo.GetAbsoluteSize();
	if (AbsSize.X <= 0 || AbsSize.Y <= 0) return false;

	if (!GEngine || !GEngine->GameViewport) return false;
	FVector2D ViewportSize;
	GEngine->GameViewport->GetViewportSize(ViewportSize);
	OutViewW = (int32)ViewportSize.X;
	OutViewH = (int32)ViewportSize.Y;

	FSceneViewport* SV = GEngine->GameViewport->GetGameViewport();
	if (!SV) return false;
	TSharedPtr<SViewport> ViewportWidget = SV->GetViewportWidget().Pin();
	if (!ViewportWidget.IsValid()) return false;

	const FGeometry VPGeo = ViewportWidget->GetCachedGeometry();
	const FVector2D VPPos = VPGeo.GetAbsolutePosition();
	const FVector2D VPSize = VPGeo.GetAbsoluteSize();
	if (VPSize.X <= 0 || VPSize.Y <= 0) return false;

	float SX = (float)OutViewW / VPSize.X;
	float SY = (float)OutViewH / VPSize.Y;
	FVector2D Rel = AbsPos - VPPos;

	OutRect.Min.X = FMath::Clamp(FMath::RoundToInt(Rel.X * SX), 0, OutViewW);
	OutRect.Min.Y = FMath::Clamp(FMath::RoundToInt(Rel.Y * SY), 0, OutViewH);
	OutRect.Max.X = FMath::Clamp(FMath::RoundToInt((Rel.X + AbsSize.X) * SX), 0, OutViewW);
	OutRect.Max.Y = FMath::Clamp(FMath::RoundToInt((Rel.Y + AbsSize.Y) * SY), 0, OutViewH);

	return OutRect.Width() > 0 && OutRect.Height() > 0;
}

// ── 清理旧截图 ───────────────────────────────────────────────────────

static void CapCleanupOldCaptures(const FString& OutputDir, int32 MaxKeep = 20)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(OutputDir / TEXT("NexusCapture_*")), true, false);
	if (Files.Num() > MaxKeep)
	{
		Files.Sort();
		const int32 ToDelete = Files.Num() - MaxKeep;
		for (int32 i = 0; i < ToDelete; ++i)
			IFileManager::Get().Delete(*(OutputDir / Files[i]), false, true, true);
	}
}

// ── 图片保存 ─────────────────────────────────────────────────────────

static bool CapSavePixels(const TArray<FColor>& Pixels, int32 W, int32 H,
	const FString& Format, const FString& FilePath)
{
	IImageWrapperModule& Mod =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Fmt = (Format == TEXT("jpg")) ? EImageFormat::JPEG : EImageFormat::PNG;
	TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(Fmt);
	if (!Wrapper.IsValid()) return false;
	if (!Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
		return false;

	const TArray64<uint8>& Data = (Fmt == EImageFormat::JPEG)
		? Wrapper->GetCompressed(85) : Wrapper->GetCompressed();
	return FFileHelper::SaveArrayToFile(Data, *FilePath);
}

// ── 统一缩放 + 保存 + 结果 ──────────────────────────────────────────

static bool CapSaveAndBuildEntry(TArray<FColor>& Pixels, int32 W, int32 H,
	int32 MaxSize, const FString& Format, const FString& FileSuffix,
	const TSharedPtr<FJsonObject>& ExtraFields,
	TSharedPtr<FJsonObject>& OutEntry, FString& OutError)
{
	TArray<FColor> ScaledPixels;
	TArray<FColor>* FinalPixels = &Pixels;
	int32 FinalW = W, FinalH = H;
	if (MaxSize > 0 && (W > MaxSize || H > MaxSize))
	{
		float Scale = (float)MaxSize / (float)FMath::Max(W, H);
		FinalW = FMath::Max(1, FMath::RoundToInt(W * Scale));
		FinalH = FMath::Max(1, FMath::RoundToInt(H * Scale));
		ScaledPixels.SetNumUninitialized(FinalW * FinalH);
		FImageUtils::ImageResize(W, H, Pixels, FinalW, FinalH, ScaledPixels, false);
		FinalPixels = &ScaledPixels;
	}

	const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("NexusCaptures");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*OutputDir);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString FileName = FString::Printf(TEXT("NexusCapture_%s_%s.%s"),
		*FileSuffix, *Timestamp, *Format);
	const FString FilePath = OutputDir / FileName;

	if (!CapSavePixels(*FinalPixels, FinalW, FinalH, Format, FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to save screenshot: %s"), *FilePath);
		return false;
	}
	CapCleanupOldCaptures(OutputDir);

	OutEntry->SetStringField(TEXT("filePath"), FilePath);
	OutEntry->SetStringField(TEXT("format"), Format);
	OutEntry->SetNumberField(TEXT("width"), FinalW);
	OutEntry->SetNumberField(TEXT("height"), FinalH);
	OutEntry->SetBoolField(TEXT("success"), true);
	if (ExtraFields.IsValid())
	{
		for (const auto& Pair : ExtraFields->Values)
			OutEntry->SetField(Pair.Key, Pair.Value);
	}
	return true;
}

// ── FNexusEditorCaptureUtils 公有方法实现 ────────────────────────────────────

const TMap<FString, FString>& FNexusEditorCaptureUtils::GetPanelTabMapping()
{
	return GetCapPanelTabMapping();
}

TArray<FNexusEditorCaptureUtils::FWindowInfo> FNexusEditorCaptureUtils::GetSortedTopLevelWindows()
{
	TArray<FWindowInfo> Result;
	for (const FCapWindowInfo& I : GetCapSortedTopLevelWindows())
	{
		FWindowInfo WI;
		WI.Window = I.Window;
		WI.Title  = I.Title;
		WI.Size   = I.Size;
		Result.Add(MoveTemp(WI));
	}
	return Result;
}

#if WITH_EDITOR
TSharedPtr<SDockTab> FNexusEditorCaptureUtils::FindPanelTab(const FString& Name) { return CapFindPanelTab(Name); }
bool FNexusEditorCaptureUtils::CaptureActorFromAngle(AActor* Actor, const FString& ViewAngle, float PaddingRatio, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	return CapCaptureActorFromAngle(Actor, ViewAngle, PaddingRatio, OutPixels, OutW, OutH);
}
#endif

bool FNexusEditorCaptureUtils::CaptureWidgetPixels(TSharedRef<SWidget> Widget, TArray<FColor>& OutPixels, int32& OutW, int32& OutH) { return CapCaptureWidgetPixels(Widget, OutPixels, OutW, OutH); }
bool FNexusEditorCaptureUtils::CropPixels(const TArray<FColor>& Src, int32 SrcW, int32 SrcH, const FIntRect& Rect, TArray<FColor>& Out, int32& OutW, int32& OutH) { return CapCropPixels(Src, SrcW, SrcH, Rect, Out, OutW, OutH); }
bool FNexusEditorCaptureUtils::CropToScreenRect(TArray<FColor>& Pixels, int32& W, int32& H, const FIntRect& ScreenRect, int32 ViewW, int32 ViewH) { return CapCropToScreenRect(Pixels, W, H, ScreenRect, ViewW, ViewH); }
void FNexusEditorCaptureUtils::GetActorVisualBounds(AActor* Actor, FVector& OutOrigin, FVector& OutExtent) { CapGetActorVisualBounds(Actor, OutOrigin, OutExtent); }
FVector FNexusEditorCaptureUtils::GetViewDirection(AActor* Actor, const FString& ViewAngle) { return CapGetViewDirection(Actor, ViewAngle); }
bool FNexusEditorCaptureUtils::GetActorScreenRect(AActor* Actor, bool bIsPIE, float Padding, FIntRect& OutRect, int32& OutViewW, int32& OutViewH) { return CapGetActorScreenRect(Actor, bIsPIE, Padding, OutRect, OutViewW, OutViewH); }
bool FNexusEditorCaptureUtils::GetUMGWidgetScreenRect(UWidget* Widget, FIntRect& OutRect, int32& OutViewW, int32& OutViewH) { return CapGetUMGWidgetScreenRect(Widget, OutRect, OutViewW, OutViewH); }
void FNexusEditorCaptureUtils::CleanupOldCaptures(const FString& OutputDir, int32 MaxKeep) { CapCleanupOldCaptures(OutputDir, MaxKeep); }
bool FNexusEditorCaptureUtils::SavePixels(const TArray<FColor>& Pixels, int32 W, int32 H, const FString& Format, const FString& FilePath) { return CapSavePixels(Pixels, W, H, Format, FilePath); }
bool FNexusEditorCaptureUtils::SaveAndBuildEntry(TArray<FColor>& Pixels, int32 W, int32 H, int32 MaxSize, const FString& Format, const FString& FileSuffix, const TSharedPtr<FJsonObject>& ExtraFields, TSharedPtr<FJsonObject>& OutEntry, FString& OutError)
{
	return CapSaveAndBuildEntry(Pixels, W, H, MaxSize, Format, FileSuffix, ExtraFields, OutEntry, OutError);
}

#endif // WITH_EDITOR
