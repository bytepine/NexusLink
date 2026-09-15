// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorCaptureUtils.h"
#include "Utils/NexusCaptureUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"
#include "RenderingThread.h"
#include "Framework/Docking/TabManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "IAssetViewport.h"
#include "SceneView.h"
#endif

// ── 面板名称 → Tab ID 映射 ────────────────────────────────────────────

const TMap<FString, FString>& FNexusEditorCaptureUtils::GetPanelTabMapping()
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

#if WITH_EDITOR

TSharedPtr<SDockTab> FNexusEditorCaptureUtils::FindPanelTab(const FString& Name)
{
	const TMap<FString, FString>& Map = GetPanelTabMapping();
	const FString* TabId = Map.Find(Name);
	// 未登记的名字按 Tab ID 原样查找，便于截任意插件面板
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

TSharedPtr<SWidget> FNexusEditorCaptureUtils::GetPanelCaptureWidget(const TSharedRef<SDockTab>& Tab)
{
	// 往上找到 SDockingTabStack 可连 tab 标题一起截；找不到就退回 tab 内容区
	TSharedPtr<SWidget> Walker = Tab->GetParentWidget();
	while (Walker.IsValid())
	{
		if (Walker->GetType().ToString() == TEXT("SDockingTabStack"))
		{
			return Walker;
		}
		Walker = Walker->GetParentWidget();
	}
	return Tab->GetContent();
}

// ── 视角方向辅助 ──────────────────────────────────────────────────

FVector FNexusEditorCaptureUtils::GetViewDirection(AActor* Actor, const FString& ViewAngle)
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

// ── Actor 屏幕包围盒（编辑器视口 SceneView 投影）────────────────────

bool FNexusEditorCaptureUtils::GetActorScreenRectInEditorViewport(AActor* Actor, float Padding,
	FIntRect& OutRect, int32& OutViewW, int32& OutViewH)
{
	if (!Actor || !GEditor) return false;

	FVector Origin, Extent;
	FNexusCaptureUtils::GetActorVisualBounds(Actor, Origin, Extent);

	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		Corners[i] = Origin + FVector(
			(i & 1) ? Extent.X : -Extent.X,
			(i & 2) ? Extent.Y : -Extent.Y,
			(i & 4) ? Extent.Z : -Extent.Z);
	}

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

	FVector2D ScreenMin(FLT_MAX, FLT_MAX);
	FVector2D ScreenMax(-FLT_MAX, -FLT_MAX);
	int32 ValidCount = 0;

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
	if (ValidCount == 0) return false;

	float PadX = (ScreenMax.X - ScreenMin.X) * Padding;
	float PadY = (ScreenMax.Y - ScreenMin.Y) * Padding;

	OutRect.Min.X = FMath::Clamp(FMath::FloorToInt(ScreenMin.X - PadX), 0, OutViewW);
	OutRect.Min.Y = FMath::Clamp(FMath::FloorToInt(ScreenMin.Y - PadY), 0, OutViewH);
	OutRect.Max.X = FMath::Clamp(FMath::CeilToInt(ScreenMax.X + PadX),  0, OutViewW);
	OutRect.Max.Y = FMath::Clamp(FMath::CeilToInt(ScreenMax.Y + PadY),  0, OutViewH);

	return OutRect.Width() > 0 && OutRect.Height() > 0;
}

// ── PIE 弹出 RAII 守卫 ───────────────────────────────────────────────
// PIE 中编辑器视口被 Game 视口顶掉，先切到 Simulate 才能动编辑器相机，析构时还原。

namespace
{
struct FNexusScopedPIEEject
{
	TSharedPtr<IAssetViewport> Viewport;
	bool bDidEject = false;

	FNexusScopedPIEEject()
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

	~FNexusScopedPIEEject()
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
}

bool FNexusEditorCaptureUtils::CaptureActorFromAngle(AActor* Actor, const FString& ViewAngle,
	float PaddingRatio, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	if (!Actor || !GEditor) return false;

	FVector Origin, Extent;
	FNexusCaptureUtils::GetActorVisualBounds(Actor, Origin, Extent);

	// 按包围盒外接球半径与 90° FOV 反推相机距离，留 PaddingRatio 的余量
	const float BoundsRadius = Extent.Size();
	const float FOV = 90.f;
	const float HalfFOVRad = FMath::DegreesToRadians(FOV * 0.5f);
	const float Distance = FMath::Max(200.f, (BoundsRadius / FMath::Tan(HalfFOVRad)) * (2.0f + PaddingRatio));

	FVector Direction = GetViewDirection(Actor, ViewAngle);
	FVector CamPos = Origin + Direction * Distance;
	FRotator CamRot = (Origin - CamPos).Rotation();

	FNexusScopedPIEEject PIEGuard;

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
	TSharedPtr<SDockTab> VPTab = FindPanelTab(TEXT("viewport"));
	if (VPTab.IsValid())
	{
		TSharedPtr<SWidget> VPContent = VPTab->GetContent();
		if (VPContent.IsValid())
			bOk = FNexusCaptureUtils::CaptureWidgetPixels(VPContent.ToSharedRef(), OutPixels, OutW, OutH);
	}

	if (bOk)
	{
		FIntRect ActorRect;
		int32 ViewW = 0, ViewH = 0;
		if (GetActorScreenRectInEditorViewport(Actor, PaddingRatio, ActorRect, ViewW, ViewH))
			FNexusCaptureUtils::CropToScreenRect(OutPixels, OutW, OutH, ActorRect, ViewW, ViewH);
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
