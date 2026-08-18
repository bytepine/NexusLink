// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusCaptureViewportCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusEditorCaptureUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "GameFramework/PlayerController.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "Widgets/Docking/SDockTab.h"
#endif

#include "NexusMcpTool.h"
// ── Cap 实现 ─────────────────────────────────────────────────────────

void FCaptureViewportCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("capture_viewport");
	Out.Description = TEXT("Capture editor/PIE/Actor/Widget. Includes editor_desktop full window.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("target"),      FNexusSchema::Str(TEXT("editor|editor_desktop|viewport|pie|<panel>|list"), TEXT("editor")))
		.Prop(TEXT("format"),      FNexusSchema::Enum(TEXT("Image format"), { TEXT("png"), TEXT("jpg") }, TEXT("png")))
		.Prop(TEXT("maxSize"),     FNexusSchema::Int(TEXT("Max edge pixels (0=native)"), 1920, 0))
		.Prop(TEXT("actorName"),   FNexusSchema::Str(TEXT("Actor name/tag; crop to screen bounds")))
		.Prop(TEXT("widgetName"),  FNexusSchema::Str(TEXT("runtime UMG Widget; target=pie")))
		.Prop(TEXT("ownerClass"),  FNexusSchema::Str(TEXT("UserWidget class filter")))
		.Prop(TEXT("padding"),     FNexusSchema::Num(TEXT("Actor bounds padding ratio"), 0.1))
		.Prop(TEXT("viewAngle"),   FNexusSchema::Enum(TEXT("Actor crop camera angle"),
			{ TEXT("front"), TEXT("back"), TEXT("left"), TEXT("right"), TEXT("top"), TEXT("bottom") }, TEXT("front")))
		.Prop(TEXT("windowIndex"), FNexusSchema::Int(TEXT("Top-level window index (0=main)"), 0, 0))
		.Prop(TEXT("validateOnly"), FNexusSchema::Bool(TEXT("If true skip image; validate target/viewport only"), false))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("screenshot"), TEXT("image"), TEXT("screen"), TEXT("snap"), TEXT("photo") };
	Out.RelatedCapabilities = { TEXT("list_runtime_widgets"), TEXT("list_runtime_actors") };
}

FCapabilityResult FCaptureViewportCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString Target     = TEXT("editor");
		FString Format     = TEXT("png");
		int32   MaxSize    = 1920;
		int32   WindowIndex = -1;
		FString ActorName;
		FString WidgetName;
		FString OwnerClass;
		FString ViewAngle;
		float   PaddingRatio = 0.1f;
		bool    bValidateOnly = false;

		if (Arguments.IsValid())
		{
			FString TmpStr;
			if (Arguments->TryGetStringField(TEXT("target"),     TmpStr)) Target    = TmpStr.ToLower();
			if (Arguments->TryGetStringField(TEXT("format"),     TmpStr)) Format    = TmpStr.ToLower();
			if (Arguments->TryGetStringField(TEXT("actorName"),  TmpStr)) ActorName = TmpStr;
			if (Arguments->TryGetStringField(TEXT("viewAngle"),  TmpStr)) ViewAngle = TmpStr.ToLower();
			if (Arguments->TryGetStringField(TEXT("widgetName"), TmpStr)) WidgetName = TmpStr;
			if (Arguments->TryGetStringField(TEXT("ownerClass"), TmpStr)) OwnerClass = TmpStr;
			if (Arguments->HasField(TEXT("maxSize")))     MaxSize     = (int32)A.Num(TEXT("maxSize"));
			if (Arguments->HasField(TEXT("windowIndex"))) WindowIndex = (int32)A.Num(TEXT("windowIndex"));
			if (Arguments->HasField(TEXT("padding")))     PaddingRatio = (float)A.Num(TEXT("padding"));
			if (Arguments->HasField(TEXT("validateOnly"))) bValidateOnly = A.Bool(TEXT("validateOnly"));
		}
		if (Format != TEXT("png") && Format != TEXT("jpg")) Format = TEXT("png");

		if (bValidateOnly)
		{
			OutEntry->SetBoolField(TEXT("validateOnly"), true);
			OutEntry->SetStringField(TEXT("target"), Target);
			if (Target == TEXT("list"))
			{
				OutEntry->SetStringField(TEXT("note"), TEXT("list mode needs no screenshot"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			if (Target == TEXT("pie"))
			{
				const bool bOk = GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid();
				OutEntry->SetBoolField(TEXT("success"), bOk);
				if (!bOk) OutEntry->SetStringField(TEXT("error"), TEXT("PIE viewport unavailable (start control_pie first)"));
			}
			else
			{
	#if WITH_EDITOR
				const bool bOk = (Target == TEXT("editor") || Target == TEXT("editor_desktop"))
					|| FNexusEditorCaptureUtils::FindPanelTab(Target).IsValid()
					|| FNexusEditorCaptureUtils::FindPanelTab(TEXT("viewport")).IsValid();
				OutEntry->SetBoolField(TEXT("success"), bOk);
				if (!bOk)
				{
					OutEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("Panel/viewport for target '%s' not found"), *Target));
				}
	#else
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("validateOnly for non-pie target requires editor build"));
	#endif
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// actorName 自动推断 target
		if (!ActorName.IsEmpty() && (Target == TEXT("editor") || Target == TEXT("editor_desktop")))
			Target = (GEngine && GEngine->GameViewport) ? TEXT("pie") : TEXT("viewport");
		if (!WidgetName.IsEmpty())
			Target = TEXT("pie");

		// list 模式：枚举可用 target
		if (Target == TEXT("list"))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			const TCHAR* Builtins[] = { TEXT("editor"), TEXT("editor_desktop"), TEXT("viewport"), TEXT("pie") };
			for (const TCHAR* T : Builtins)
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), T);
				P->SetStringField(TEXT("tabId"), TEXT("-"));
				P->SetBoolField(TEXT("open"), true);
				Arr.Add(MakeShared<FJsonValueObject>(P));
			}
			for (const auto& Pair : FNexusEditorCaptureUtils::GetPanelTabMapping())
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), Pair.Key);
				P->SetStringField(TEXT("tabId"), Pair.Value);
	#if WITH_EDITOR
				P->SetBoolField(TEXT("open"), FNexusEditorCaptureUtils::FindPanelTab(Pair.Key).IsValid());
	#else
				P->SetBoolField(TEXT("open"), false);
	#endif
				Arr.Add(MakeShared<FJsonValueObject>(P));
			}
			OutEntry->SetArrayField(TEXT("panels"), Arr);

			TArray<TSharedPtr<FJsonValue>> WinArr;
			TArray<FNexusEditorCaptureUtils::FWindowInfo> SortedWindows = FNexusEditorCaptureUtils::GetSortedTopLevelWindows();
			for (int32 i = 0; i < SortedWindows.Num(); ++i)
			{
				const FNexusEditorCaptureUtils::FWindowInfo& WI = SortedWindows[i];
				TSharedPtr<FJsonObject> W = MakeShared<FJsonObject>();
				W->SetNumberField(TEXT("index"), i);
				W->SetStringField(TEXT("title"), WI.Title);
				W->SetNumberField(TEXT("width"), WI.Size.X);
				W->SetNumberField(TEXT("height"), WI.Size.Y);
				WinArr.Add(MakeShared<FJsonValueObject>(W));
			}
			OutEntry->SetArrayField(TEXT("windows"), WinArr);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// viewAngle 模式：移动编辑器相机拍摄 Actor
		if (!ActorName.IsEmpty() && !ViewAngle.IsEmpty())
		{
	#if WITH_EDITOR
			UWorld* World = nullptr;
			if (GEditor->IsPlayingSessionInEditor())
			{
				FWorldContext* PIECtx = GEditor->GetPIEWorldContext();
				if (PIECtx) World = PIECtx->World();
			}
			if (!World) World = GEditor->GetEditorWorldContext().World();
			AActor* Actor = World ? FNexusRuntimeUtils::FindActorByName(World, ActorName) : nullptr;
			if (!Actor)
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			TArray<FColor> Pixels;
			int32 W = 0, H = 0;
			if (!FNexusEditorCaptureUtils::CaptureActorFromAngle(Actor, ViewAngle, PaddingRatio, Pixels, W, H))
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("Editor viewport screenshot failed"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("actorName"), ActorName);
			Extra->SetStringField(TEXT("viewAngle"), ViewAngle);
			FString Suffix = FString::Printf(TEXT("%s_%s"), *ViewAngle, *ActorName);
			FString SaveErr;
			FNexusEditorCaptureUtils::SaveAndBuildEntry(Pixels, W, H, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr);
			if (!SaveErr.IsEmpty())
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), SaveErr);
			}
	#else
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), TEXT("viewAngle only available in editor mode"));
	#endif
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// ── 按 target 采集像素 ──
		TArray<FColor> CapturedPixels;
		int32 CapturedW = 0, CapturedH = 0;

		auto SetError = [&](const FString& Msg)
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), Msg);
		};

		if (Target == TEXT("pie"))
		{
			if (!GEngine || !GEngine->GameViewport)
			{
				SetError(TEXT("No PIE viewport (call control_pie start first)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			TSharedPtr<SViewport> GameVPWidget = GEngine->GameViewport->GetGameViewportWidget();
			if (!GameVPWidget.IsValid() ||
				!FNexusEditorCaptureUtils::CaptureWidgetPixels(GameVPWidget.ToSharedRef(), CapturedPixels, CapturedW, CapturedH))
			{
				SetError(TEXT("PIE viewport screenshot failed"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
		}
		else if (Target == TEXT("viewport"))
		{
	#if WITH_EDITOR
			TSharedPtr<SDockTab> VPTab = FNexusEditorCaptureUtils::FindPanelTab(TEXT("viewport"));
			if (!VPTab.IsValid()) { SetError(TEXT("Editor viewport tab not found")); OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry)); return; }
			TSharedPtr<SWidget> VPContent = VPTab->GetContent();
			if (!VPContent.IsValid() ||
				!FNexusEditorCaptureUtils::CaptureWidgetPixels(VPContent.ToSharedRef(), CapturedPixels, CapturedW, CapturedH))
			{
				SetError(TEXT("Editor viewport screenshot failed"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
	#else
			SetError(TEXT("Editor viewport screenshot editor mode only"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
	#endif
		}
		else if (Target == TEXT("editor") || Target == TEXT("editor_desktop"))
		{
			TArray<FNexusEditorCaptureUtils::FWindowInfo> SortedWindows = FNexusEditorCaptureUtils::GetSortedTopLevelWindows();
			TSharedPtr<SWindow> TargetWindow;

			if (WindowIndex >= 0)
			{
				if (WindowIndex < SortedWindows.Num())
					TargetWindow = SortedWindows[WindowIndex].Window;
				else
				{
					SetError(FString::Printf(
						TEXT("windowIndex %d out of range; %d visible windows available (0-%d). "
						     "Note: minimized windows cannot be captured; restore the window and retry."),
						WindowIndex, SortedWindows.Num(), FMath::Max(0, SortedWindows.Num() - 1)));
					OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
					return;
				}
			}
			else
			{
				TargetWindow = SortedWindows.Num() > 0
					? SortedWindows[0].Window
					: FSlateApplication::Get().GetActiveTopLevelWindow();
			}

			if (!TargetWindow.IsValid())
			{
				SetError(TEXT("Target editor window not found. Restore minimized window and retry."));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			if (!FNexusEditorCaptureUtils::CaptureWidgetPixels(TargetWindow.ToSharedRef(), CapturedPixels, CapturedW, CapturedH))
			{
				SetError(TEXT("Editor window screenshot failed (FWidgetRenderer)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
		}
		else
		{
	#if WITH_EDITOR
			TSharedPtr<SDockTab> Tab = FNexusEditorCaptureUtils::FindPanelTab(Target);
			if (!Tab.IsValid())
			{
				SetError(FString::Printf(
					TEXT("Panel '%s' not found; use target='list' for available panels"), *Target));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			TSharedPtr<SWidget> PanelWidget = Tab->GetContent();
			TSharedPtr<SWidget> Walker = Tab->GetParentWidget();
			while (Walker.IsValid())
			{
				if (Walker->GetType().ToString() == TEXT("SDockingTabStack"))
				{
					PanelWidget = Walker;
					break;
				}
				Walker = Walker->GetParentWidget();
			}
			if (!PanelWidget.IsValid() ||
				!FNexusEditorCaptureUtils::CaptureWidgetPixels(PanelWidget.ToSharedRef(), CapturedPixels, CapturedW, CapturedH))
			{
				SetError(FString::Printf(TEXT("Panel '%s' screenshot failed"), *Target));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
	#else
			SetError(TEXT("Panel screenshot editor mode only"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
	#endif
		}

		// ── Actor / Widget 二次裁切 ──
		int32 FinalW = CapturedW, FinalH = CapturedH;
		TArray<FColor>* FinalPixels = &CapturedPixels;
		TArray<FColor> SubCroppedPixels;

		if (!ActorName.IsEmpty())
		{
			bool bIsPIE = (Target == TEXT("pie"));
			UWorld* World = nullptr;
			if (bIsPIE)
			{
				World = FNexusRuntimeUtils::GetActiveWorld();
			}
#if WITH_EDITOR
			else if (GEditor)
			{
				World = GEditor->GetEditorWorldContext().World();
			}
#endif

			AActor* Actor = World ? FNexusRuntimeUtils::FindActorByName(World, ActorName) : nullptr;
			if (!Actor) { SetError(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry)); return; }

			FIntRect ActorRect;
			int32 ViewW = 0, ViewH = 0;
			if (!FNexusEditorCaptureUtils::GetActorScreenRect(Actor, bIsPIE, PaddingRatio, ActorRect, ViewW, ViewH))
			{
				SetError(FString::Printf(
					TEXT("Actor '%s' off-screen or projection failed"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			FNexusEditorCaptureUtils::CropToScreenRect(*FinalPixels, FinalW, FinalH, ActorRect, ViewW, ViewH);
			SubCroppedPixels = MoveTemp(*FinalPixels);
			FinalPixels = &SubCroppedPixels;
		}
		else if (!WidgetName.IsEmpty())
		{
			UWidget* TargetWidget = FNexusRuntimeUtils::FindRuntimeWidget(OwnerClass, WidgetName);
			if (!TargetWidget)
			{
				SetError(FString::Printf(TEXT("runtime UMG Widget not found: %s"), *WidgetName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			FIntRect WidgetRect;
			int32 ViewW = 0, ViewH = 0;
			if (!FNexusEditorCaptureUtils::GetUMGWidgetScreenRect(TargetWidget, WidgetRect, ViewW, ViewH))
			{
				SetError(FString::Printf(TEXT("Widget '%s' has no screen geometry (may be invisible)"), *WidgetName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			FNexusEditorCaptureUtils::CropToScreenRect(*FinalPixels, FinalW, FinalH, WidgetRect, ViewW, ViewH);
			SubCroppedPixels = MoveTemp(*FinalPixels);
			FinalPixels = &SubCroppedPixels;
		}

		// ── 缩放 + 保存 + 填写结果 ──
		FString Suffix = Target;
		if (!ActorName.IsEmpty())
			Suffix = FString::Printf(TEXT("%s_%s"), *Target, *ActorName);
		else if (!WidgetName.IsEmpty())
			Suffix = FString::Printf(TEXT("%s_%s"), *Target, *WidgetName);

		TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
		Extra->SetStringField(TEXT("target"), Target);

		FString SaveErr;
		if (!FNexusEditorCaptureUtils::SaveAndBuildEntry(*FinalPixels, FinalW, FinalH, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr))
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), SaveErr);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCaptureViewportCapability)

#endif // WITH_EDITOR
