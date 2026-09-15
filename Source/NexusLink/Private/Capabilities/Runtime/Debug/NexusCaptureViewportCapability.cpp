// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Debug/NexusCaptureViewportCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusCaptureUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "NexusMcpTool.h"

void FCaptureViewportCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("capture_viewport");
	Out.Description = TEXT("Capture Game/PIE viewport or top-level window; optional Actor/UMG crop. No LevelEditor panels or viewAngle.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("target"),      FNexusSchema::Str(TEXT("editor|editor_desktop|viewport|pie|list"), TEXT("editor")))
		.Prop(TEXT("format"),      FNexusSchema::Enum(TEXT("Image format"), { TEXT("png"), TEXT("jpg") }, TEXT("png")))
		.Prop(TEXT("maxSize"),     FNexusSchema::Int(TEXT("Max edge pixels (0=native)"), 1920, 0))
		.Prop(TEXT("actorName"),   FNexusSchema::Str(TEXT("Actor name/tag; crop to screen bounds")))
		.Prop(TEXT("widgetName"),  FNexusSchema::Str(TEXT("runtime UMG Widget; uses Game viewport")))
		.Prop(TEXT("ownerClass"),  FNexusSchema::Str(TEXT("UserWidget class filter")))
		.Prop(TEXT("padding"),     FNexusSchema::Num(TEXT("Actor bounds padding ratio"), 0.1))
		.Prop(TEXT("windowIndex"), FNexusSchema::Int(TEXT("Top-level window index; omit for first/active")))
		.Prop(TEXT("validateOnly"), FNexusSchema::Bool(TEXT("If true skip image; validate target/viewport only"), false))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("screenshot"), TEXT("image"), TEXT("screen"), TEXT("snap"), TEXT("photo") };
	Out.RelatedCapabilities = { TEXT("capture_editor_panel"), TEXT("list_runtime_widgets"), TEXT("list_runtime_actors") };
	Out.WhenToUse = TEXT("Screenshot Game/PIE viewport or the game window for visual debug");
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

		const bool bHasGameViewport = GEngine && GEngine->GameViewport
			&& (GEngine->GameViewport->Viewport || GEngine->GameViewport->GetGameViewport());
		if (Target == TEXT("viewport") && bHasGameViewport)
		{
			Target = TEXT("pie");
		}

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
			if (Target == TEXT("pie") || Target == TEXT("viewport"))
			{
				const bool bOk = bHasGameViewport;
				OutEntry->SetBoolField(TEXT("success"), bOk);
				if (!bOk) OutEntry->SetStringField(TEXT("error"), TEXT("Game/PIE viewport unavailable"));
			}
			else
			{
				const bool bOk = (Target == TEXT("editor") || Target == TEXT("editor_desktop"))
					&& (FNexusCaptureUtils::GetSortedTopLevelWindows().Num() > 0 || bHasGameViewport);
				OutEntry->SetBoolField(TEXT("success"), bOk);
				if (!bOk)
				{
					OutEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("No capturable window/viewport for target '%s'"), *Target));
				}
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (!ActorName.IsEmpty() && (Target == TEXT("editor") || Target == TEXT("editor_desktop")))
			Target = bHasGameViewport ? TEXT("pie") : Target;
		if (!WidgetName.IsEmpty())
			Target = TEXT("pie");

		if (!ActorName.IsEmpty() && !ViewAngle.IsEmpty())
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"),
				TEXT("viewAngle requires LevelEditor camera and is not available at Runtime; omit viewAngle and crop the current Game/PIE view"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (Target == TEXT("list"))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			const TCHAR* Builtins[] = { TEXT("editor"), TEXT("editor_desktop"), TEXT("viewport"), TEXT("pie") };
			for (const TCHAR* T : Builtins)
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), T);
				P->SetStringField(TEXT("tabId"), TEXT("-"));
				P->SetBoolField(TEXT("open"),
					(FCString::Stricmp(T, TEXT("pie")) == 0 || FCString::Stricmp(T, TEXT("viewport")) == 0)
						? bHasGameViewport
						: true);
				Arr.Add(MakeShared<FJsonValueObject>(P));
			}
			OutEntry->SetArrayField(TEXT("panels"), Arr);

			TArray<TSharedPtr<FJsonValue>> WinArr;
			TArray<FNexusCaptureUtils::FWindowInfo> SortedWindows = FNexusCaptureUtils::GetSortedTopLevelWindows();
			for (int32 i = 0; i < SortedWindows.Num(); ++i)
			{
				const FNexusCaptureUtils::FWindowInfo& WI = SortedWindows[i];
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

		TArray<FColor> CapturedPixels;
		int32 CapturedW = 0, CapturedH = 0;

		auto SetError = [&](const FString& Msg)
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), Msg);
		};

		if (Target == TEXT("pie") || Target == TEXT("viewport"))
		{
			if (!FNexusCaptureUtils::CaptureGameViewportPixels(CapturedPixels, CapturedW, CapturedH))
			{
				SetError(TEXT("Game/PIE viewport screenshot failed (no GameViewport or ReadPixels)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			Target = TEXT("pie");
		}
		else if (Target == TEXT("editor") || Target == TEXT("editor_desktop"))
		{
			TArray<FNexusCaptureUtils::FWindowInfo> SortedWindows = FNexusCaptureUtils::GetSortedTopLevelWindows();
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
					: (FSlateApplication::IsInitialized()
						? FSlateApplication::Get().GetActiveTopLevelWindow()
						: TSharedPtr<SWindow>());
			}

			if (!TargetWindow.IsValid())
			{
				if (FNexusCaptureUtils::CaptureGameViewportPixels(CapturedPixels, CapturedW, CapturedH))
				{
					Target = TEXT("pie");
				}
				else
				{
					SetError(TEXT("Target window not found. Restore minimized window and retry."));
					OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
					return;
				}
			}
			else if (!FNexusCaptureUtils::CaptureWidgetPixels(TargetWindow.ToSharedRef(), CapturedPixels, CapturedW, CapturedH))
			{
				if (!FNexusCaptureUtils::CaptureGameViewportPixels(CapturedPixels, CapturedW, CapturedH))
				{
					SetError(TEXT("Window screenshot failed (FWidgetRenderer) and GameViewport fallback failed"));
					OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
					return;
				}
				Target = TEXT("pie");
			}
		}
		else
		{
			SetError(FString::Printf(
				TEXT("Panel '%s' not found; Runtime capture supports editor|editor_desktop|viewport|pie|list (no LevelEditor tabs)"),
				*Target));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		int32 FinalW = CapturedW, FinalH = CapturedH;
		TArray<FColor>* FinalPixels = &CapturedPixels;
		TArray<FColor> SubCroppedPixels;

		if (!ActorName.IsEmpty())
		{
			UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
			AActor* Actor = World ? FNexusRuntimeUtils::FindActorByName(World, ActorName) : nullptr;
			if (!Actor)
			{
				SetError(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FIntRect ActorRect;
			int32 ViewW = 0, ViewH = 0;
			if (!FNexusCaptureUtils::GetActorScreenRect(Actor, PaddingRatio, ActorRect, ViewW, ViewH))
			{
				SetError(FString::Printf(TEXT("Actor '%s' off-screen or projection failed"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			FNexusCaptureUtils::CropToScreenRect(*FinalPixels, FinalW, FinalH, ActorRect, ViewW, ViewH);
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
			if (!FNexusCaptureUtils::GetUMGWidgetScreenRect(TargetWidget, WidgetRect, ViewW, ViewH))
			{
				SetError(FString::Printf(TEXT("Widget '%s' has no screen geometry (may be invisible)"), *WidgetName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			FNexusCaptureUtils::CropToScreenRect(*FinalPixels, FinalW, FinalH, WidgetRect, ViewW, ViewH);
			SubCroppedPixels = MoveTemp(*FinalPixels);
			FinalPixels = &SubCroppedPixels;
		}

		FString Suffix = Target;
		if (!ActorName.IsEmpty())
			Suffix = FString::Printf(TEXT("%s_%s"), *Target, *ActorName);
		else if (!WidgetName.IsEmpty())
			Suffix = FString::Printf(TEXT("%s_%s"), *Target, *WidgetName);

		TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
		Extra->SetStringField(TEXT("target"), Target);

		FString SaveErr;
		if (!FNexusCaptureUtils::SaveAndBuildEntry(*FinalPixels, FinalW, FinalH, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr))
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), SaveErr);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCaptureViewportCapability)
