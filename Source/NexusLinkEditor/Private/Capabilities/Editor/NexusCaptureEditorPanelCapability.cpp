// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusCaptureEditorPanelCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusCaptureUtils.h"
#include "Utils/NexusEditorCaptureUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor.h"
#include "Widgets/Docking/SDockTab.h"
#include "NexusMcpTool.h"

void FNexusCaptureEditorPanelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CaptureEditorPanel");
	Out.Description = TEXT("Screenshot a LevelEditor panel tab, or an Actor from a fixed camera angle. Editor only; use CaptureViewport for Game/PIE.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Target"),    FNexusSchema::Str(TEXT("Panel name or Tab ID; 'list' enumerates"), TEXT("Viewport")))
		.Prop(TEXT("Format"),    FNexusSchema::Enum(TEXT("Image format"), { TEXT("png"), TEXT("jpg") }, TEXT("png")))
		.Prop(TEXT("MaxSize"),   FNexusSchema::Int(TEXT("Max edge pixels (0=native)"), 1920, 0))
		.Prop(TEXT("bIncludeImage"), FNexusSchema::Bool(TEXT("Return standard MCP image content with FilePath metadata"), true, true))
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor name/tag; crop to editor-viewport bounds")))
		.Prop(TEXT("Padding"),   FNexusSchema::Num(TEXT("Actor bounds padding ratio"), 0.1))
		.Prop(TEXT("ViewAngle"), FNexusSchema::Enum(TEXT("Move editor camera to this angle; needs ActorName"),
			{ TEXT("Front"), TEXT("Back"), TEXT("Left"), TEXT("Right"), TEXT("Top"), TEXT("Bottom") }))
		.Prop(TEXT("bValidateOnly"), FNexusSchema::Bool(TEXT("If true skip image; validate panel only"), false))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.bCacheable = false;
	Out.ExtraSearchKeywords = { TEXT("screenshot"), TEXT("panel"), TEXT("tab"), TEXT("image"), TEXT("outliner"), TEXT("details") };
	Out.RelatedCapabilities = { TEXT("CaptureViewport"), TEXT("GetEditorContext") };
	Out.WhenToUse = TEXT("Screenshot an editor panel/level viewport, or an Actor from a fixed angle");
}

FCapabilityResult FNexusCaptureEditorPanelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FCapabilityResult Result = FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString Target    = TEXT("Viewport");
		FString Format    = TEXT("png");
		int32   MaxSize   = 1920;
		FString ActorName;
		FString ViewAngle;
		float   PaddingRatio  = 0.1f;
		bool    bValidateOnly = false;

		FString TmpStr;
		if (Arguments->TryGetStringField(TEXT("Target"),    TmpStr)) Target    = TmpStr.ToLower();
		if (Arguments->TryGetStringField(TEXT("Format"),    TmpStr)) Format    = TmpStr.ToLower();
		if (Arguments->TryGetStringField(TEXT("ActorName"), TmpStr)) ActorName = TmpStr;
		if (Arguments->TryGetStringField(TEXT("ViewAngle"), TmpStr)) ViewAngle = TmpStr.ToLower();
		if (Arguments->HasField(TEXT("MaxSize")))      MaxSize       = (int32)A.Num(TEXT("MaxSize"));
		if (Arguments->HasField(TEXT("Padding")))      PaddingRatio  = (float)A.Num(TEXT("Padding"));
		if (Arguments->HasField(TEXT("bValidateOnly"))) bValidateOnly = A.Bool(TEXT("bValidateOnly"));
		if (Format != TEXT("png") && Format != TEXT("jpg")) Format = TEXT("png");

		auto SetError = [&](const FString& Msg)
		{
			OutEntry->SetBoolField(TEXT("bSuccess"), false);
			OutEntry->SetStringField(TEXT("Error"), Msg);
		};

		// ── list 模式：枚举登记面板与当前是否打开 ──
		if (Target == TEXT("list"))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const TPair<FString, FString>& Pair : FNexusEditorCaptureUtils::GetPanelTabMapping())
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"),  Pair.Key);
				P->SetStringField(TEXT("TabId"), Pair.Value);
				P->SetBoolField(TEXT("bOpen"), FNexusEditorCaptureUtils::FindPanelTab(Pair.Key).IsValid());
				Arr.Add(MakeShared<FJsonValueObject>(P));
			}
			OutEntry->SetArrayField(TEXT("Panels"), Arr);
			OutEntry->SetStringField(TEXT("Note"),
				TEXT("Unlisted Tab IDs also work; Game/PIE viewport and full windows go through CaptureViewport"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// ViewAngle 必须配 ActorName，否则请求本身就是矛盾的
		if (!ViewAngle.IsEmpty() && ActorName.IsEmpty())
		{
			SetError(TEXT("ViewAngle requires ActorName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (bValidateOnly)
		{
			OutEntry->SetBoolField(TEXT("bValidateOnly"), true);
			OutEntry->SetStringField(TEXT("Target"), Target);
			const bool bOk = FNexusEditorCaptureUtils::FindPanelTab(Target).IsValid();
			OutEntry->SetBoolField(TEXT("bSuccess"), bOk);
			if (!bOk)
			{
				OutEntry->SetStringField(TEXT("Error"),
					FString::Printf(TEXT("Panel '%s' not open; use Target='list' for available panels"), *Target));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// Actor 定位：PIE 会话优先取 PIE World，否则编辑器 World
		UWorld* EditorOrPieWorld = nullptr;
		if (!ActorName.IsEmpty())
		{
			if (GEditor->IsPlayingSessionInEditor())
			{
				if (FWorldContext* PIECtx = GEditor->GetPIEWorldContext()) EditorOrPieWorld = PIECtx->World();
			}
			if (!EditorOrPieWorld) EditorOrPieWorld = GEditor->GetEditorWorldContext().World();
		}

		// ── ViewAngle 模式：移动编辑器相机环绕拍摄 ──
		if (!ActorName.IsEmpty() && !ViewAngle.IsEmpty())
		{
			AActor* Actor = EditorOrPieWorld
				? FNexusRuntimeUtils::FindActorByName(EditorOrPieWorld, ActorName) : nullptr;
			if (!Actor)
			{
				SetError(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			TArray<FColor> Pixels;
			int32 W = 0, H = 0;
			if (!FNexusEditorCaptureUtils::CaptureActorFromAngle(Actor, ViewAngle, PaddingRatio, Pixels, W, H))
			{
				SetError(TEXT("Editor viewport screenshot failed (no perspective level viewport?)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("ActorName"), ActorName);
			Extra->SetStringField(TEXT("ViewAngle"), ViewAngle);
			const FString Suffix = FString::Printf(TEXT("%s_%s"), *ViewAngle, *ActorName);

			FString SaveErr;
			if (!FNexusCaptureUtils::SaveAndBuildEntry(Pixels, W, H, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr))
			{
				SetError(SaveErr);
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// ── 面板 tab 截图 ──
		TSharedPtr<SDockTab> Tab = FNexusEditorCaptureUtils::FindPanelTab(Target);
		if (!Tab.IsValid())
		{
			SetError(FString::Printf(
				TEXT("Panel '%s' not found; use Target='list' for available panels"), *Target));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		TArray<FColor> CapturedPixels;
		int32 CapturedW = 0, CapturedH = 0;
		TSharedPtr<SWidget> PanelWidget = FNexusEditorCaptureUtils::GetPanelCaptureWidget(Tab.ToSharedRef());
		if (!PanelWidget.IsValid() ||
			!FNexusCaptureUtils::CaptureWidgetPixels(PanelWidget.ToSharedRef(), CapturedPixels, CapturedW, CapturedH))
		{
			SetError(FString::Printf(TEXT("Panel '%s' screenshot failed"), *Target));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// ── Actor 二次裁切（无 ViewAngle：就地按编辑器相机投影裁）──
		int32 FinalW = CapturedW, FinalH = CapturedH;
		TArray<FColor>* FinalPixels = &CapturedPixels;
		TArray<FColor> SubCroppedPixels;

		if (!ActorName.IsEmpty())
		{
			AActor* Actor = EditorOrPieWorld
				? FNexusRuntimeUtils::FindActorByName(EditorOrPieWorld, ActorName) : nullptr;
			if (!Actor)
			{
				SetError(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FIntRect ActorRect;
			int32 ViewW = 0, ViewH = 0;
			if (!FNexusEditorCaptureUtils::GetActorScreenRectInEditorViewport(
					Actor, PaddingRatio, ActorRect, ViewW, ViewH))
			{
				SetError(FString::Printf(TEXT("Actor '%s' off-screen or projection failed"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			FNexusCaptureUtils::CropToScreenRect(*FinalPixels, FinalW, FinalH, ActorRect, ViewW, ViewH);
			SubCroppedPixels = MoveTemp(*FinalPixels);
			FinalPixels = &SubCroppedPixels;
		}

		FString Suffix = Target;
		if (!ActorName.IsEmpty()) Suffix = FString::Printf(TEXT("%s_%s"), *Target, *ActorName);

		TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
		Extra->SetStringField(TEXT("Target"), Target);

		FString SaveErr;
		if (!FNexusCaptureUtils::SaveAndBuildEntry(
				*FinalPixels, FinalW, FinalH, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr))
		{
			SetError(SaveErr);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
	if (FNexusArgs(Arguments).Bool(TEXT("bIncludeImage"), true)) FNexusCaptureUtils::AttachGeneratedImages(Result);
	return Result;
}

REGISTER_MCP_CAPABILITY(FNexusCaptureEditorPanelCapability)

#endif // WITH_EDITOR
