// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusManageAssetUserWidgetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusWidgetAnimationUtils.h"
#include "Utils/NexusPropertyUtils.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#endif
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "NexusMcpTool.h"

void FNexusManageAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetUserWidget");
	Out.bInjectCompile = true;
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Batch edit WBP: widget tree/slots/props and animation tracks; EventGraph via ManageAssetBlueprint.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Widget operation"),
			{ TEXT("Add"), TEXT("Remove"), TEXT("SetSlot"), TEXT("SetProperty"),
			  TEXT("AddAnimation"), TEXT("RemoveAnimation"), TEXT("AddTrack"), TEXT("AddKey"),
			  TEXT("RemoveTrack"), TEXT("RemoveKey") }))
		.Prop(TEXT("WidgetClass"),  FNexusSchema::Str(TEXT("Widget class short name (add)")))
		.Prop(TEXT("WidgetName"),   FNexusSchema::Str(TEXT("Widget name; required for remove/set_*/animation track")))
		.Prop(TEXT("ParentWidget"), FNexusSchema::Str(TEXT("Parent panel Widget name (add)")))
		.Prop(TEXT("AnimationName"), FNexusSchema::Str(TEXT("Animation name (add/RemoveAnimation/track/key)")))
		.Prop(TEXT("TrackName"),    FNexusSchema::Str(TEXT("Float track display name (add/RemoveTrack/key)")))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path (SetProperty; e.g. RenderOpacity for AddTrack)")))
		.Prop(TEXT("Time"),         FNexusSchema::Num(TEXT("Keyframe time seconds (AddKey/RemoveKey)")))
		.Prop(TEXT("KeyValue"),     FNexusSchema::Num(TEXT("Float keyframe value (AddKey)")))
		.Prop(TEXT("Value"),        FNexusSchema::Str(TEXT("Property value (SetProperty)")))
		.Prop(TEXT("AnchorMinX"),   FNexusSchema::Num(TEXT("Canvas anchor MinX (SetSlot)")))
		.Prop(TEXT("AnchorMinY"),   FNexusSchema::Num(TEXT("Canvas anchor MinY (SetSlot)")))
		.Prop(TEXT("AnchorMaxX"),   FNexusSchema::Num(TEXT("Canvas anchor MaxX (SetSlot)")))
		.Prop(TEXT("AnchorMaxY"),   FNexusSchema::Num(TEXT("Canvas anchor MaxY (SetSlot)")))
		.Prop(TEXT("AlignmentX"),   FNexusSchema::Num(TEXT("Alignment X (SetSlot)")))
		.Prop(TEXT("AlignmentY"),   FNexusSchema::Num(TEXT("Alignment Y (SetSlot)")))
		.Prop(TEXT("OffsetLeft"),   FNexusSchema::Num(TEXT("offset Left (SetSlot)")))
		.Prop(TEXT("OffsetTop"),    FNexusSchema::Num(TEXT("offset Top (SetSlot)")))
		.Prop(TEXT("OffsetRight"),  FNexusSchema::Num(TEXT("offset Right (SetSlot)")))
		.Prop(TEXT("OffsetBottom"), FNexusSchema::Num(TEXT("offset Bottom (SetSlot)")))
		.Required({ TEXT("Action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("WidgetBlueprint asset path (shared)")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch widget ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("wbp"), TEXT("umg"), TEXT("Children"), TEXT("UI"), TEXT("slot"), TEXT("Animation"), TEXT("Key")
	};
	Out.RelatedCapabilities = {
		TEXT("GetAssetUserWidget"), TEXT("CreateAssetUserWidget"), TEXT("SaveAsset"),
		TEXT("GetAssetBlueprint"), TEXT("ManageAssetBlueprint")
	};
	Out.WhenToUse = TEXT("Widget tree/animation tracks use this cap; EventGraph via ManageAssetBlueprint");
}

#if WITH_EDITOR
struct FUserWidgetActionState
{
	UWidgetBlueprint* WBP = nullptr;
	bool bDidMutate = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FUserWidgetActionState* WBPState(FNexusActionContext& Ctx)
{
	return static_cast<FUserWidgetActionState*>(Ctx.Target);
}

static UWidgetBlueprint* WBPFrom(FNexusActionContext& Ctx)
{
	FUserWidgetActionState* S = WBPState(Ctx);
	return S ? S->WBP : nullptr;
}

static void MarkWBPMutated(FNexusActionContext& Ctx)
{
	if (FUserWidgetActionState* S = WBPState(Ctx))
	{
		S->bDidMutate = true;
	}
}

static void HandleWBP_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	if (!WBP)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("WidgetBlueprint is null"));
		return;
	}
	const FString WidgetName = FNexusArgs(Op).Str(TEXT("WidgetName"));
	if (WidgetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Action=remove requires WidgetName"));
		return;
	}
	Ctx.Entry->SetStringField(TEXT("WidgetName"), WidgetName);
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
		return;
	}
	WBP->WidgetTree->Modify();
	if (Target->Slot && Target->Slot->Parent) Target->Slot->Parent->RemoveChild(Target);
	else if (WBP->WidgetTree->RootWidget == Target) WBP->WidgetTree->RootWidget = nullptr;
	Target->Rename(nullptr, GetTransientPackage());
#if NX_UE_HAS_MARK_AS_GARBAGE
	Target->MarkAsGarbage();
#else
	Target->MarkPendingKill();
#endif
	MarkWBPMutated(Ctx);
}

static void HandleWBP_Add(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	if (!WBP)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("WidgetBlueprint is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString WidgetClass = A.Str(TEXT("WidgetClass"));
	if (WidgetClass.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Action=add requires WidgetClass"));
		return;
	}
	FString WidgetName = A.Str(TEXT("WidgetName"));
	const FString ParentName = A.Str(TEXT("ParentWidget"));

	UClass* NewClass = FNexusAssetUtils::FindClassWithUPrefix(WidgetClass);
	if (!NewClass || !NewClass->IsChildOf(UWidget::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Widget class not found: %s"), *WidgetClass));
		return;
	}
	if (WidgetName.IsEmpty())
		WidgetName = FString::Printf(TEXT("%s_%d"), *WidgetClass, FMath::Rand() % 10000);

	WBP->WidgetTree->SetFlags(RF_Transactional);
	WBP->WidgetTree->Modify();
	UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(NewClass, FName(*WidgetName));
	if (!NewWidget)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Create Widget failed: %s"), *WidgetClass));
		return;
	}

	FString AttachedTo;
	if (!ParentName.IsEmpty())
	{
		UPanelWidget* Panel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(FName(*ParentName)));
		if (!Panel)
		{
			NewWidget->Rename(nullptr, GetTransientPackage());
#if NX_UE_HAS_MARK_AS_GARBAGE
			NewWidget->MarkAsGarbage();
#else
			NewWidget->MarkPendingKill();
#endif
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Parent panel Widget not found: %s"), *ParentName));
			return;
		}
		Panel->AddChild(NewWidget);
		AttachedTo = ParentName;
	}
	else
	{
		if (!WBP->WidgetTree->RootWidget) WBP->WidgetTree->RootWidget = NewWidget;
		AttachedTo = TEXT("(root)");
	}

	Ctx.Entry->SetStringField(TEXT("WidgetName"),  NewWidget->GetName());
	Ctx.Entry->SetStringField(TEXT("WidgetClass"), NewClass->GetName());
	Ctx.Entry->SetStringField(TEXT("AttachedTo"),  AttachedTo);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_SetSlot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	if (!WBP)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("WidgetBlueprint is null"));
		return;
	}
	const FString WidgetName = FNexusArgs(Op).Str(TEXT("WidgetName"));
	if (WidgetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Action=SetSlot requires WidgetName"));
		return;
	}
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
		return;
	}
	FString SlotErr;
	if (!FNexusWidgetLayoutUtils::ApplyCanvasSlotFields(Target, Op, SlotErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), SlotErr);
		return;
	}
	WBP->WidgetTree->Modify();
	Target->Modify();
	Ctx.Entry->SetStringField(TEXT("WidgetName"), WidgetName);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	if (!WBP)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("WidgetBlueprint is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString WidgetName = A.Str(TEXT("WidgetName"));
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (WidgetName.IsEmpty() || PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires WidgetName、PropertyPath、value"));
		return;
	}
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
		return;
	}
	FString OldVal, ActualVal, PropErr;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Target, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), PropErr);
		return;
	}
	WBP->WidgetTree->Modify();
	Target->Modify();
	Ctx.Entry->SetStringField(TEXT("WidgetName"), WidgetName);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_AddAnimation(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FString AnimName = FNexusArgs(Op).Str(TEXT("AnimationName"));
	if (AnimName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddAnimation requires AnimationName"));
		return;
	}
	FString AnimErr;
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::AddAnimation(WBP, AnimName, AnimErr);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("AnimationName"), Anim->GetName());
	MarkWBPMutated(Ctx);
}

static void HandleWBP_RemoveAnimation(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FString AnimName = FNexusArgs(Op).Str(TEXT("AnimationName"));
	if (AnimName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveAnimation requires AnimationName"));
		return;
	}
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::RemoveAnimation(WBP, AnimName, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("AnimationName"), AnimName);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_AddTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("AnimationName"));
	const FString TrackName = A.Str(TEXT("TrackName"));
	const FString WidgetName = A.Str(TEXT("WidgetName"));
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTrack requires existing AnimationName"));
		return;
	}
	FString OutTrack, AnimErr;
	const bool bOk = WidgetName.IsEmpty()
		? FNexusWidgetAnimationUtils::AddFloatTrack(Anim, TrackName, OutTrack, AnimErr)
		: FNexusWidgetAnimationUtils::AddBoundFloatTrack(Anim, WBP, WidgetName, PropPath, TrackName, OutTrack, AnimErr);
	if (!bOk)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("AnimationName"), Anim->GetName());
	Ctx.Entry->SetStringField(TEXT("TrackName"), OutTrack);
	if (!WidgetName.IsEmpty()) Ctx.Entry->SetStringField(TEXT("WidgetName"), WidgetName);
	if (!PropPath.IsEmpty()) Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_AddKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("AnimationName"));
	const FString TrackName = A.Str(TEXT("TrackName"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddKey requires existing AnimationName"));
		return;
	}
	if (!Op->HasField(TEXT("Time")) || !Op->HasField(TEXT("KeyValue")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddKey requires time and KeyValue"));
		return;
	}
	const float TimeSec = static_cast<float>(A.Num(TEXT("Time")));
	const float KeyVal  = static_cast<float>(A.Num(TEXT("KeyValue")));
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::AddFloatKey(Anim, TrackName, TimeSec, KeyVal, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("AnimationName"), Anim->GetName());
	if (!TrackName.IsEmpty()) Ctx.Entry->SetStringField(TEXT("TrackName"), TrackName);
	Ctx.Entry->SetNumberField(TEXT("Time"), TimeSec);
	Ctx.Entry->SetNumberField(TEXT("KeyValue"), KeyVal);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_RemoveTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("AnimationName"));
	const FString TrackName = A.Str(TEXT("TrackName"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTrack requires existing AnimationName"));
		return;
	}
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::RemoveFloatTrack(Anim, TrackName, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("AnimationName"), Anim->GetName());
	Ctx.Entry->SetStringField(TEXT("TrackName"), TrackName);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("AnimationName"));
	const FString TrackName = A.Str(TEXT("TrackName"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveKey requires existing AnimationName"));
		return;
	}
	if (!Op->HasField(TEXT("Time")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveKey requires time"));
		return;
	}
	const float TimeSec = static_cast<float>(A.Num(TEXT("Time")));
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::RemoveFloatKey(Anim, TrackName, TimeSec, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("AnimationName"), Anim->GetName());
	Ctx.Entry->SetNumberField(TEXT("Time"), TimeSec);
	MarkWBPMutated(Ctx);
}
#endif // WITH_EDITOR

bool FNexusManageAssetUserWidgetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UWidgetBlueprint* WBP = FNexusAssetEditorUtils::LoadWidgetBP(AssetPath);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath);
		return false;
	}
	if (!WBP->WidgetTree)
	{
		OutError = TEXT("WidgetTree unavailable");
		return false;
	}
	FUserWidgetActionState* State = new FUserWidgetActionState();
	State->WBP = WBP;
	OutTarget = State;
	return true;
#else
	OutError = TEXT("ManageAssetUserWidget only available in editor builds");
	return false;
#endif
}

void FNexusManageAssetUserWidgetCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
#if WITH_EDITOR
	(void)Args;
	if (FUserWidgetActionState* State = static_cast<FUserWidgetActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
#else
	(void)Target;
	(void)Args;
	(void)OutTop;
#endif
}

void FNexusManageAssetUserWidgetCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FUserWidgetActionState* State = static_cast<FUserWidgetActionState*>(Target);
	if (!State)
	{
		return;
	}
	if (State->bDidMutate && State->WBP)
	{
		State->WBP->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Hint"), TEXT("Call SaveAsset to persist changes"));
		}
	}
	delete State;
#else
	(void)Target;
#endif
}

void FNexusManageAssetUserWidgetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("Remove"),           &HandleWBP_Remove);
	OutHandlers.Add(TEXT("Add"),              &HandleWBP_Add);
	OutHandlers.Add(TEXT("SetSlot"),         &HandleWBP_SetSlot);
	OutHandlers.Add(TEXT("SetProperty"),     &HandleWBP_SetProperty);
	OutHandlers.Add(TEXT("AddAnimation"),    &HandleWBP_AddAnimation);
	OutHandlers.Add(TEXT("RemoveAnimation"), &HandleWBP_RemoveAnimation);
	OutHandlers.Add(TEXT("AddTrack"),        &HandleWBP_AddTrack);
	OutHandlers.Add(TEXT("AddKey"),          &HandleWBP_AddKey);
	OutHandlers.Add(TEXT("RemoveTrack"),     &HandleWBP_RemoveTrack);
	OutHandlers.Add(TEXT("RemoveKey"),       &HandleWBP_RemoveKey);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetUserWidgetCapability)


