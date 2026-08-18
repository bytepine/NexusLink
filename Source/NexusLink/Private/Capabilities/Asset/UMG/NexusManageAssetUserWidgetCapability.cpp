// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusManageAssetUserWidgetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
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

void FManageAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_user_widget");
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Batch edit WBP: widget tree/slots/props and animation tracks; EventGraph via manage_asset_blueprint.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Widget operation"),
			{ TEXT("add"), TEXT("remove"), TEXT("set_slot"), TEXT("set_property"),
			  TEXT("add_animation"), TEXT("remove_animation"), TEXT("add_track"), TEXT("add_key"),
			  TEXT("remove_track"), TEXT("remove_key") }))
		.Prop(TEXT("widgetClass"),  FNexusSchema::Str(TEXT("Widget class short name (add)")))
		.Prop(TEXT("widgetName"),   FNexusSchema::Str(TEXT("Widget name; required for remove/set_*/animation track")))
		.Prop(TEXT("parentWidget"), FNexusSchema::Str(TEXT("Parent panel Widget name (add)")))
		.Prop(TEXT("animationName"), FNexusSchema::Str(TEXT("Animation name (add/remove_animation/track/key)")))
		.Prop(TEXT("trackName"),    FNexusSchema::Str(TEXT("Float track display name (add/remove_track/key)")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property; e.g. RenderOpacity for add_track)")))
		.Prop(TEXT("time"),         FNexusSchema::Num(TEXT("Keyframe time seconds (add_key/remove_key)")))
		.Prop(TEXT("keyValue"),     FNexusSchema::Num(TEXT("Float keyframe value (add_key)")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("Property value (set_property)")))
		.Prop(TEXT("anchorMinX"),   FNexusSchema::Num(TEXT("Canvas anchor minX (set_slot)")))
		.Prop(TEXT("anchorMinY"),   FNexusSchema::Num(TEXT("Canvas anchor minY (set_slot)")))
		.Prop(TEXT("anchorMaxX"),   FNexusSchema::Num(TEXT("Canvas anchor maxX (set_slot)")))
		.Prop(TEXT("anchorMaxY"),   FNexusSchema::Num(TEXT("Canvas anchor maxY (set_slot)")))
		.Prop(TEXT("alignmentX"),   FNexusSchema::Num(TEXT("Alignment X (set_slot)")))
		.Prop(TEXT("alignmentY"),   FNexusSchema::Num(TEXT("Alignment Y (set_slot)")))
		.Prop(TEXT("offsetLeft"),   FNexusSchema::Num(TEXT("offset Left (set_slot)")))
		.Prop(TEXT("offsetTop"),    FNexusSchema::Num(TEXT("offset Top (set_slot)")))
		.Prop(TEXT("offsetRight"),  FNexusSchema::Num(TEXT("offset Right (set_slot)")))
		.Prop(TEXT("offsetBottom"), FNexusSchema::Num(TEXT("offset Bottom (set_slot)")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("WidgetBlueprint asset path (shared)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch widget ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("wbp"), TEXT("umg"), TEXT("children"), TEXT("ui"), TEXT("slot"), TEXT("animation"), TEXT("key")
	};
	Out.RelatedCapabilities = {
		TEXT("get_asset_user_widget"), TEXT("create_asset_user_widget"), TEXT("save_asset"),
		TEXT("get_asset_blueprint"), TEXT("manage_asset_blueprint")
	};
	Out.WhenToUse = TEXT("Widget tree/animation tracks use this cap; EventGraph via manage_asset_blueprint");
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
	const FString WidgetName = FNexusArgs(Op).Str(TEXT("widgetName"));
	if (WidgetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("action=remove requires widgetName"));
		return;
	}
	Ctx.Entry->SetStringField(TEXT("widgetName"), WidgetName);
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
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
	const FNexusArgs A(Op);
	const FString WidgetClass = A.Str(TEXT("widgetClass"));
	if (WidgetClass.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("action=add requires widgetClass"));
		return;
	}
	FString WidgetName = A.Str(TEXT("widgetName"));
	const FString ParentName = A.Str(TEXT("parentWidget"));

	UClass* NewClass = FNexusAssetUtils::FindClassWithUPrefix(WidgetClass);
	if (!NewClass || !NewClass->IsChildOf(UWidget::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget class not found: %s"), *WidgetClass));
		return;
	}
	if (WidgetName.IsEmpty())
		WidgetName = FString::Printf(TEXT("%s_%d"), *WidgetClass, FMath::Rand() % 10000);

	WBP->WidgetTree->SetFlags(RF_Transactional);
	WBP->WidgetTree->Modify();
	UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(NewClass, FName(*WidgetName));
	if (!NewWidget)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Create Widget failed: %s"), *WidgetClass));
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
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent panel Widget not found: %s"), *ParentName));
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

	Ctx.Entry->SetStringField(TEXT("widgetName"),  NewWidget->GetName());
	Ctx.Entry->SetStringField(TEXT("widgetClass"), NewClass->GetName());
	Ctx.Entry->SetStringField(TEXT("attachedTo"),  AttachedTo);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_SetSlot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FString WidgetName = FNexusArgs(Op).Str(TEXT("widgetName"));
	if (WidgetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("action=set_slot requires widgetName"));
		return;
	}
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
		return;
	}
	FString SlotErr;
	if (!FNexusWidgetLayoutUtils::ApplyCanvasSlotFields(Target, Op, SlotErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), SlotErr);
		return;
	}
	WBP->WidgetTree->Modify();
	Target->Modify();
	Ctx.Entry->SetStringField(TEXT("widgetName"), WidgetName);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString WidgetName = A.Str(TEXT("widgetName"));
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (WidgetName.IsEmpty() || PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires widgetName、propertyPath、value"));
		return;
	}
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
		return;
	}
	FString OldVal, ActualVal, PropErr;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Target, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), PropErr);
		return;
	}
	WBP->WidgetTree->Modify();
	Target->Modify();
	Ctx.Entry->SetStringField(TEXT("widgetName"), WidgetName);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_AddAnimation(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FString AnimName = FNexusArgs(Op).Str(TEXT("animationName"));
	if (AnimName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_animation requires animationName"));
		return;
	}
	FString AnimErr;
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::AddAnimation(WBP, AnimName, AnimErr);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("animationName"), Anim->GetName());
	MarkWBPMutated(Ctx);
}

static void HandleWBP_RemoveAnimation(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FString AnimName = FNexusArgs(Op).Str(TEXT("animationName"));
	if (AnimName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_animation requires animationName"));
		return;
	}
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::RemoveAnimation(WBP, AnimName, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("animationName"), AnimName);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_AddTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("animationName"));
	const FString TrackName = A.Str(TEXT("trackName"));
	const FString WidgetName = A.Str(TEXT("widgetName"));
	const FString PropPath = A.Str(TEXT("propertyPath"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_track requires existing animationName"));
		return;
	}
	FString OutTrack, AnimErr;
	const bool bOk = WidgetName.IsEmpty()
		? FNexusWidgetAnimationUtils::AddFloatTrack(Anim, TrackName, OutTrack, AnimErr)
		: FNexusWidgetAnimationUtils::AddBoundFloatTrack(Anim, WBP, WidgetName, PropPath, TrackName, OutTrack, AnimErr);
	if (!bOk)
	{
		Ctx.Entry->SetStringField(TEXT("error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("animationName"), Anim->GetName());
	Ctx.Entry->SetStringField(TEXT("trackName"), OutTrack);
	if (!WidgetName.IsEmpty()) Ctx.Entry->SetStringField(TEXT("widgetName"), WidgetName);
	if (!PropPath.IsEmpty()) Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_AddKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("animationName"));
	const FString TrackName = A.Str(TEXT("trackName"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_key requires existing animationName"));
		return;
	}
	if (!Op->HasField(TEXT("time")) || !Op->HasField(TEXT("keyValue")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_key requires time and keyValue"));
		return;
	}
	const float TimeSec = static_cast<float>(A.Num(TEXT("time")));
	const float KeyVal  = static_cast<float>(A.Num(TEXT("keyValue")));
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::AddFloatKey(Anim, TrackName, TimeSec, KeyVal, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("animationName"), Anim->GetName());
	if (!TrackName.IsEmpty()) Ctx.Entry->SetStringField(TEXT("trackName"), TrackName);
	Ctx.Entry->SetNumberField(TEXT("time"), TimeSec);
	Ctx.Entry->SetNumberField(TEXT("keyValue"), KeyVal);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_RemoveTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("animationName"));
	const FString TrackName = A.Str(TEXT("trackName"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_track requires existing animationName"));
		return;
	}
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::RemoveFloatTrack(Anim, TrackName, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("animationName"), Anim->GetName());
	Ctx.Entry->SetStringField(TEXT("trackName"), TrackName);
	MarkWBPMutated(Ctx);
}

static void HandleWBP_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWidgetBlueprint* WBP = WBPFrom(Ctx);
	const FNexusArgs A(Op);
	const FString AnimName = A.Str(TEXT("animationName"));
	const FString TrackName = A.Str(TEXT("trackName"));
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_key requires existing animationName"));
		return;
	}
	if (!Op->HasField(TEXT("time")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_key requires time"));
		return;
	}
	const float TimeSec = static_cast<float>(A.Num(TEXT("time")));
	FString AnimErr;
	if (!FNexusWidgetAnimationUtils::RemoveFloatKey(Anim, TrackName, TimeSec, AnimErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), AnimErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("animationName"), Anim->GetName());
	Ctx.Entry->SetNumberField(TEXT("time"), TimeSec);
	MarkWBPMutated(Ctx);
}
#endif // WITH_EDITOR

bool FManageAssetUserWidgetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
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
	OutError = TEXT("manage_asset_user_widget only available in editor builds");
	return false;
#endif
}

void FManageAssetUserWidgetCapability::AfterPrepareTarget(
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

void FManageAssetUserWidgetCapability::FinalizeTarget(void* Target) const
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
			State->OutTop->SetStringField(TEXT("hint"), TEXT("Call save_asset to persist changes"));
		}
	}
	delete State;
#else
	(void)Target;
#endif
}

void FManageAssetUserWidgetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("remove"),           &HandleWBP_Remove);
	OutHandlers.Add(TEXT("add"),              &HandleWBP_Add);
	OutHandlers.Add(TEXT("set_slot"),         &HandleWBP_SetSlot);
	OutHandlers.Add(TEXT("set_property"),     &HandleWBP_SetProperty);
	OutHandlers.Add(TEXT("add_animation"),    &HandleWBP_AddAnimation);
	OutHandlers.Add(TEXT("remove_animation"), &HandleWBP_RemoveAnimation);
	OutHandlers.Add(TEXT("add_track"),        &HandleWBP_AddTrack);
	OutHandlers.Add(TEXT("add_key"),          &HandleWBP_AddKey);
	OutHandlers.Add(TEXT("remove_track"),     &HandleWBP_RemoveTrack);
	OutHandlers.Add(TEXT("remove_key"),       &HandleWBP_RemoveKey);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetUserWidgetCapability)


