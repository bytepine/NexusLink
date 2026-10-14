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

void FNexusCaptureViewportCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CaptureViewport");
	Out.Description = TEXT("Capture Game/PIE viewport or top-level window; optional Actor/UMG crop. No LevelEditor panels or ViewAngle.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Target"),      FNexusSchema::Str(TEXT("editor|EditorDesktop|viewport|pie|list"), TEXT("Editor")))
		.Prop(TEXT("Format"),      FNexusSchema::Enum(TEXT("Image format"), { TEXT("png"), TEXT("jpg") }, TEXT("png")))
		.Prop(TEXT("MaxSize"),     FNexusSchema::Int(TEXT("Max edge pixels (0=native)"), 1920, 0))
		.Prop(TEXT("ActorName"),   FNexusSchema::Str(TEXT("Actor name/tag; crop to screen bounds")))
		.Prop(TEXT("WidgetName"),  FNexusSchema::Str(TEXT("runtime UMG Widget; uses Game viewport")))
		.Prop(TEXT("OwnerClass"),  FNexusSchema::Str(TEXT("UserWidget class filter")))
		.Prop(TEXT("Padding"),     FNexusSchema::Num(TEXT("Actor bounds padding ratio"), 0.1))
		.Prop(TEXT("WindowIndex"), FNexusSchema::Int(TEXT("Top-level window index; omit for first/active")))
		.Prop(TEXT("bValidateOnly"), FNexusSchema::Bool(TEXT("If true skip image; validate target/viewport only"), false))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("screenshot"), TEXT("image"), TEXT("screen"), TEXT("snap"), TEXT("photo") };
	Out.RelatedCapabilities = { TEXT("CaptureEditorPanel"), TEXT("ListRuntimeWidgets"), TEXT("ListRuntimeActors") };
	Out.WhenToUse = TEXT("Screenshot Game/PIE viewport or the game window for visual debug");
}

FCapabilityResult FNexusCaptureViewportCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString Target     = TEXT("Editor");
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
			if (Arguments->TryGetStringField(TEXT("Target"),     TmpStr)) Target    = TmpStr.ToLower();
			if (Arguments->TryGetStringField(TEXT("Format"),     TmpStr)) Format    = TmpStr.ToLower();
			if (Arguments->TryGetStringField(TEXT("ActorName"),  TmpStr)) ActorName = TmpStr;
			if (Arguments->TryGetStringField(TEXT("ViewAngle"),  TmpStr)) ViewAngle = TmpStr.ToLower();
			if (Arguments->TryGetStringField(TEXT("WidgetName"), TmpStr)) WidgetName = TmpStr;
			if (Arguments->TryGetStringField(TEXT("OwnerClass"), TmpStr)) OwnerClass = TmpStr;
			if (Arguments->HasField(TEXT("MaxSize")))     MaxSize     = (int32)A.Num(TEXT("MaxSize"));
			if (Arguments->HasField(TEXT("WindowIndex"))) WindowIndex = (int32)A.Num(TEXT("WindowIndex"));
			if (Arguments->HasField(TEXT("Padding")))     PaddingRatio = (float)A.Num(TEXT("Padding"));
			if (Arguments->HasField(TEXT("bValidateOnly"))) bValidateOnly = A.Bool(TEXT("bValidateOnly"));
		}
		if (Format != TEXT("png") && Format != TEXT("jpg")) Format = TEXT("png");

		const bool bHasGameViewport = GEngine && GEngine->GameViewport
			&& (GEngine->GameViewport->Viewport || GEngine->GameViewport->GetGameViewport());
		if (Target == TEXT("Viewport") && bHasGameViewport)
		{
			Target = TEXT("PIE");
		}

		if (bValidateOnly)
		{
			OutEntry->SetBoolField(TEXT("bValidateOnly"), true);
			OutEntry->SetStringField(TEXT("Target"), Target);
			if (Target == TEXT("list"))
			{
				OutEntry->SetStringField(TEXT("Note"), TEXT("list mode needs no screenshot"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			if (Target == TEXT("PIE") || Target == TEXT("Viewport"))
			{
				const bool bOk = bHasGameViewport;
				OutEntry->SetBoolField(TEXT("bSuccess"), bOk);
				if (!bOk) OutEntry->SetStringField(TEXT("Error"), TEXT("Game/PIE viewport unavailable"));
			}
			else
			{
				const bool bOk = (Target == TEXT("Editor") || Target == TEXT("EditorDesktop"))
					&& (FNexusCaptureUtils::GetSortedTopLevelWindows().Num() > 0 || bHasGameViewport);
				OutEntry->SetBoolField(TEXT("bSuccess"), bOk);
				if (!bOk)
				{
					OutEntry->SetStringField(TEXT("Error"),
						FString::Printf(TEXT("No capturable window/viewport for target '%s'"), *Target));
				}
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (!ActorName.IsEmpty() && (Target == TEXT("Editor") || Target == TEXT("EditorDesktop")))
			Target = bHasGameViewport ? TEXT("PIE") : Target;
		if (!WidgetName.IsEmpty())
			Target = TEXT("PIE");

		if (!ActorName.IsEmpty() && !ViewAngle.IsEmpty())
		{
			OutEntry->SetBoolField(TEXT("bSuccess"), false);
			OutEntry->SetStringField(TEXT("Error"),
				TEXT("ViewAngle requires LevelEditor camera and is not available at Runtime; omit ViewAngle and crop the current Game/PIE view"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (Target == TEXT("list"))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			const TCHAR* Builtins[] = { TEXT("Editor"), TEXT("EditorDesktop"), TEXT("Viewport"), TEXT("PIE") };
			for (const TCHAR* T : Builtins)
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), T);
				P->SetStringField(TEXT("TabId"), TEXT("-"));
				P->SetBoolField(TEXT("bOpen"),
					(FCString::Stricmp(T, TEXT("PIE")) == 0 || FCString::Stricmp(T, TEXT("Viewport")) == 0)
						? bHasGameViewport
						: true);
				Arr.Add(MakeShared<FJsonValueObject>(P));
			}
			OutEntry->SetArrayField(TEXT("Panels"), Arr);

			TArray<TSharedPtr<FJsonValue>> WinArr;
			TArray<FNexusCaptureUtils::FWindowInfo> SortedWindows = FNexusCaptureUtils::GetSortedTopLevelWindows();
			for (int32 SortedWindowIndex = 0; SortedWindowIndex < SortedWindows.Num(); ++SortedWindowIndex)
			{
				const FNexusCaptureUtils::FWindowInfo& WI = SortedWindows[SortedWindowIndex];
				TSharedPtr<FJsonObject> W = MakeShared<FJsonObject>();
				W->SetNumberField(TEXT("Index"), SortedWindowIndex);
				W->SetStringField(TEXT("Title"), WI.Title);
				W->SetNumberField(TEXT("Width"), WI.Size.X);
				W->SetNumberField(TEXT("Height"), WI.Size.Y);
				WinArr.Add(MakeShared<FJsonValueObject>(W));
			}
			OutEntry->SetArrayField(TEXT("Windows"), WinArr);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		TArray<FColor> CapturedPixels;
		int32 CapturedW = 0, CapturedH = 0;

		auto SetError = [&](const FString& Msg)
		{
			OutEntry->SetBoolField(TEXT("bSuccess"), false);
			OutEntry->SetStringField(TEXT("Error"), Msg);
		};

		if (Target == TEXT("PIE") || Target == TEXT("Viewport"))
		{
			if (!FNexusCaptureUtils::CaptureGameViewportPixels(CapturedPixels, CapturedW, CapturedH))
			{
				SetError(TEXT("Game/PIE viewport screenshot failed (no GameViewport or ReadPixels)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			Target = TEXT("PIE");
		}
		else if (Target == TEXT("Editor") || Target == TEXT("EditorDesktop"))
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
						TEXT("WindowIndex %d out of range; %d visible windows available (0-%d). "
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
					Target = TEXT("PIE");
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
				Target = TEXT("PIE");
			}
		}
		else
		{
			SetError(FString::Printf(
				TEXT("Panel '%s' not found; Runtime capture supports editor|EditorDesktop|viewport|pie|list (no LevelEditor tabs)"),
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
		Extra->SetStringField(TEXT("Target"), Target);

		FString SaveErr;
		if (!FNexusCaptureUtils::SaveAndBuildEntry(*FinalPixels, FinalW, FinalH, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr))
		{
			OutEntry->SetBoolField(TEXT("bSuccess"), false);
			OutEntry->SetStringField(TEXT("Error"), SaveErr);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCaptureViewportCapability)
