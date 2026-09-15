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

void FCaptureEditorPanelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("capture_editor_panel");
	Out.Description = TEXT("Screenshot a LevelEditor panel tab, or an Actor from a fixed camera angle. Editor only; use capture_viewport for Game/PIE.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("target"),    FNexusSchema::Str(TEXT("Panel name or Tab ID; 'list' enumerates"), TEXT("viewport")))
		.Prop(TEXT("format"),    FNexusSchema::Enum(TEXT("Image format"), { TEXT("png"), TEXT("jpg") }, TEXT("png")))
		.Prop(TEXT("maxSize"),   FNexusSchema::Int(TEXT("Max edge pixels (0=native)"), 1920, 0))
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Actor name/tag; crop to editor-viewport bounds")))
		.Prop(TEXT("padding"),   FNexusSchema::Num(TEXT("Actor bounds padding ratio"), 0.1))
		.Prop(TEXT("viewAngle"), FNexusSchema::Enum(TEXT("Move editor camera to this angle; needs actorName"),
			{ TEXT("front"), TEXT("back"), TEXT("left"), TEXT("right"), TEXT("top"), TEXT("bottom") }, TEXT("front")))
		.Prop(TEXT("validateOnly"), FNexusSchema::Bool(TEXT("If true skip image; validate panel only"), false))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("screenshot"), TEXT("panel"), TEXT("tab"), TEXT("image"), TEXT("outliner"), TEXT("details") };
	Out.RelatedCapabilities = { TEXT("capture_viewport"), TEXT("get_editor_context") };
	Out.WhenToUse = TEXT("Screenshot an editor panel/level viewport, or an Actor from a fixed angle");
}

FCapabilityResult FCaptureEditorPanelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString Target    = TEXT("viewport");
		FString Format    = TEXT("png");
		int32   MaxSize   = 1920;
		FString ActorName;
		FString ViewAngle;
		float   PaddingRatio  = 0.1f;
		bool    bValidateOnly = false;

		FString TmpStr;
		if (Arguments->TryGetStringField(TEXT("target"),    TmpStr)) Target    = TmpStr.ToLower();
		if (Arguments->TryGetStringField(TEXT("format"),    TmpStr)) Format    = TmpStr.ToLower();
		if (Arguments->TryGetStringField(TEXT("actorName"), TmpStr)) ActorName = TmpStr;
		if (Arguments->TryGetStringField(TEXT("viewAngle"), TmpStr)) ViewAngle = TmpStr.ToLower();
		if (Arguments->HasField(TEXT("maxSize")))      MaxSize       = (int32)A.Num(TEXT("maxSize"));
		if (Arguments->HasField(TEXT("padding")))      PaddingRatio  = (float)A.Num(TEXT("padding"));
		if (Arguments->HasField(TEXT("validateOnly"))) bValidateOnly = A.Bool(TEXT("validateOnly"));
		if (Format != TEXT("png") && Format != TEXT("jpg")) Format = TEXT("png");

		auto SetError = [&](const FString& Msg)
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), Msg);
		};

		// ── list 模式：枚举登记面板与当前是否打开 ──
		if (Target == TEXT("list"))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const TPair<FString, FString>& Pair : FNexusEditorCaptureUtils::GetPanelTabMapping())
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"),  Pair.Key);
				P->SetStringField(TEXT("tabId"), Pair.Value);
				P->SetBoolField(TEXT("open"), FNexusEditorCaptureUtils::FindPanelTab(Pair.Key).IsValid());
				Arr.Add(MakeShared<FJsonValueObject>(P));
			}
			OutEntry->SetArrayField(TEXT("panels"), Arr);
			OutEntry->SetStringField(TEXT("note"),
				TEXT("Unlisted Tab IDs also work; Game/PIE viewport and full windows go through capture_viewport"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// viewAngle 必须配 actorName，否则请求本身就是矛盾的
		if (!ViewAngle.IsEmpty() && ActorName.IsEmpty())
		{
			SetError(TEXT("viewAngle requires actorName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (bValidateOnly)
		{
			OutEntry->SetBoolField(TEXT("validateOnly"), true);
			OutEntry->SetStringField(TEXT("target"), Target);
			const bool bOk = FNexusEditorCaptureUtils::FindPanelTab(Target).IsValid();
			OutEntry->SetBoolField(TEXT("success"), bOk);
			if (!bOk)
			{
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("Panel '%s' not open; use target='list' for available panels"), *Target));
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

		// ── viewAngle 模式：移动编辑器相机环绕拍摄 ──
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
			Extra->SetStringField(TEXT("actorName"), ActorName);
			Extra->SetStringField(TEXT("viewAngle"), ViewAngle);
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
				TEXT("Panel '%s' not found; use target='list' for available panels"), *Target));
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

		// ── Actor 二次裁切（无 viewAngle：就地按编辑器相机投影裁）──
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
		Extra->SetStringField(TEXT("target"), Target);

		FString SaveErr;
		if (!FNexusCaptureUtils::SaveAndBuildEntry(
				*FinalPixels, FinalW, FinalH, MaxSize, Format, Suffix, Extra, OutEntry, SaveErr))
		{
			SetError(SaveErr);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCaptureEditorPanelCapability)

#endif // WITH_EDITOR
