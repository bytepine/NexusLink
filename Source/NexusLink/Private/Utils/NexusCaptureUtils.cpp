// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusCaptureUtils.h"
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
#include "Widgets/SWidget.h"
#include "Widgets/SViewport.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "Components/Widget.h"
#include "RenderingThread.h"

#if !UE_SERVER
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#endif

bool FNexusCaptureUtils::CaptureGameViewportPixels(TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return false;
	}

	FViewport* VP = GEngine->GameViewport->Viewport;
	if (!VP)
	{
		FSceneViewport* SV = GEngine->GameViewport->GetGameViewport();
		VP = SV;
	}
	if (!VP) return false;

	const FIntPoint Size = VP->GetSizeXY();
	OutW = Size.X;
	OutH = Size.Y;
	if (OutW <= 0 || OutH <= 0) return false;

	FlushRenderingCommands();
	if (!VP->ReadPixels(OutPixels)) return false;
	return OutPixels.Num() == OutW * OutH;
}

bool FNexusCaptureUtils::CaptureWidgetPixels(TSharedRef<SWidget> Widget,
	TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
#if UE_SERVER
	return false;
#else
	if (!FSlateApplication::IsInitialized()) return false;

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
	const bool bOK = RTRes->ReadPixels(OutPixels, ReadFlags);
	RT->ConditionalBeginDestroy();
	return bOK && OutPixels.Num() == OutW * OutH;
#endif
}

TArray<FNexusCaptureUtils::FWindowInfo> FNexusCaptureUtils::GetSortedTopLevelWindows()
{
	TArray<FWindowInfo> Result;
	if (!FSlateApplication::IsInitialized()) return Result;

	TArray<TSharedRef<SWindow>> TopWindows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(TopWindows);
	for (const TSharedRef<SWindow>& Win : TopWindows)
	{
		if (!Win->IsRegularWindow()) continue;
		FWindowInfo Info;
		Info.Window = Win;
		Info.Title  = Win->GetTitle().ToString();
		Info.Size   = Win->GetSizeInScreen();
		Result.Add(Info);
	}
	Result.Sort([](const FWindowInfo& A, const FWindowInfo& B) {
		return (A.Size.X * A.Size.Y) > (B.Size.X * B.Size.Y);
	});
	return Result;
}

bool FNexusCaptureUtils::CropPixels(const TArray<FColor>& Src, int32 SrcW, int32 SrcH,
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

bool FNexusCaptureUtils::CropToScreenRect(TArray<FColor>& Pixels, int32& W, int32& H,
	const FIntRect& ScreenRect, int32 ViewW, int32 ViewH)
{
	const float RatioX = (float)W / (float)FMath::Max(1, ViewW);
	const float RatioY = (float)H / (float)FMath::Max(1, ViewH);
	FIntRect PixelRect;
	PixelRect.Min.X = FMath::Clamp(FMath::FloorToInt(ScreenRect.Min.X * RatioX), 0, W);
	PixelRect.Min.Y = FMath::Clamp(FMath::FloorToInt(ScreenRect.Min.Y * RatioY), 0, H);
	PixelRect.Max.X = FMath::Clamp(FMath::CeilToInt(ScreenRect.Max.X * RatioX),  0, W);
	PixelRect.Max.Y = FMath::Clamp(FMath::CeilToInt(ScreenRect.Max.Y * RatioY),  0, H);

	TArray<FColor> Cropped;
	int32 CW = 0, CH = 0;
	if (CropPixels(Pixels, W, H, PixelRect, Cropped, CW, CH))
	{
		Pixels = MoveTemp(Cropped);
		W = CW;
		H = CH;
		return true;
	}
	return false;
}

void FNexusCaptureUtils::GetActorVisualBounds(AActor* Actor, FVector& OutOrigin, FVector& OutExtent)
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

bool FNexusCaptureUtils::GetActorScreenRect(AActor* Actor, float Padding,
	FIntRect& OutRect, int32& OutViewW, int32& OutViewH)
{
	if (!Actor) return false;

	UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
	if (!World) return false;
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return false;

	int32 VPW = 0, VPH = 0;
	PC->GetViewportSize(VPW, VPH);
	OutViewW = VPW;
	OutViewH = VPH;
	if (OutViewW <= 0 || OutViewH <= 0) return false;

	FVector Origin, Extent;
	GetActorVisualBounds(Actor, Origin, Extent);

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
	if (ValidCount == 0) return false;

	const float PadX = (ScreenMax.X - ScreenMin.X) * Padding;
	const float PadY = (ScreenMax.Y - ScreenMin.Y) * Padding;
	OutRect.Min.X = FMath::Clamp(FMath::FloorToInt(ScreenMin.X - PadX), 0, OutViewW);
	OutRect.Min.Y = FMath::Clamp(FMath::FloorToInt(ScreenMin.Y - PadY), 0, OutViewH);
	OutRect.Max.X = FMath::Clamp(FMath::CeilToInt(ScreenMax.X + PadX),  0, OutViewW);
	OutRect.Max.Y = FMath::Clamp(FMath::CeilToInt(ScreenMax.Y + PadY),  0, OutViewH);
	return OutRect.Width() > 0 && OutRect.Height() > 0;
}

bool FNexusCaptureUtils::GetUMGWidgetScreenRect(UWidget* Widget, FIntRect& OutRect,
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

	const float SX = (float)OutViewW / VPSize.X;
	const float SY = (float)OutViewH / VPSize.Y;
	const FVector2D Rel = AbsPos - VPPos;

	OutRect.Min.X = FMath::Clamp(FMath::RoundToInt(Rel.X * SX), 0, OutViewW);
	OutRect.Min.Y = FMath::Clamp(FMath::RoundToInt(Rel.Y * SY), 0, OutViewH);
	OutRect.Max.X = FMath::Clamp(FMath::RoundToInt((Rel.X + AbsSize.X) * SX), 0, OutViewW);
	OutRect.Max.Y = FMath::Clamp(FMath::RoundToInt((Rel.Y + AbsSize.Y) * SY), 0, OutViewH);
	return OutRect.Width() > 0 && OutRect.Height() > 0;
}

void FNexusCaptureUtils::CleanupOldCaptures(const FString& OutputDir, int32 MaxKeep)
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

bool FNexusCaptureUtils::SavePixels(const TArray<FColor>& Pixels, int32 W, int32 H,
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

bool FNexusCaptureUtils::SaveAndBuildEntry(TArray<FColor>& Pixels, int32 W, int32 H,
	int32 MaxSize, const FString& Format, const FString& FileSuffix,
	const TSharedPtr<FJsonObject>& ExtraFields,
	TSharedPtr<FJsonObject>& OutEntry, FString& OutError)
{
	TArray<FColor> ScaledPixels;
	TArray<FColor>* FinalPixels = &Pixels;
	int32 FinalW = W, FinalH = H;
	if (MaxSize > 0 && (W > MaxSize || H > MaxSize))
	{
		const float Scale = (float)MaxSize / (float)FMath::Max(W, H);
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

	if (!SavePixels(*FinalPixels, FinalW, FinalH, Format, FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to save screenshot: %s"), *FilePath);
		return false;
	}
	CleanupOldCaptures(OutputDir);

	OutEntry->SetStringField(TEXT("filePath"), FilePath);
	OutEntry->SetStringField(TEXT("format"), Format);
	OutEntry->SetNumberField(TEXT("width"), FinalW);
	OutEntry->SetNumberField(TEXT("height"), FinalH);
	if (ExtraFields.IsValid())
	{
		for (const auto& Pair : ExtraFields->Values)
			OutEntry->SetField(Pair.Key, Pair.Value);
	}
	return true;
}
