// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimSequenceCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/AnimDataModel.h"
#endif
#include "Animation/AnimCurveTypes.h"
#include "UObject/UnrealType.h"
#include "NexusMcpTool.h"

// ── 跨版本帧率设置辅助 ──────────────────────────────────────────────────────
// UE4.26–5.5：帧率通过反射设置（AnimSequence 无公开 SetFrameRate）
// UE5.6+：通过 IAnimationDataController

static bool SetAnimSequenceFrameRate(UAnimSequence* Seq, float NewFrameRate)
{
	if (!Seq || NewFrameRate <= 0.f) return false;

#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
	IAnimationDataController& Controller = Seq->GetController();
	Controller.SetFrameRate(FFrameRate(static_cast<int32>(NewFrameRate), 1));
	return true;
#else
	// 反射设置帧率：UE4/UE5 早期版本通过 SamplingFrameRate 或 NumFrames + SequenceLength 推算
	{
		FProperty* FRProp = FindFProperty<FProperty>(Seq->GetClass(), TEXT("SamplingFrameRate"));
		if (FStructProperty* SP = CastField<FStructProperty>(FRProp))
		{
			if (SP->Struct && SP->Struct->GetFName() == FName(TEXT("FrameRate")))
			{
				FFrameRate* FR = SP->ContainerPtrToValuePtr<FFrameRate>(Seq);
				if (FR) { FR->Numerator = static_cast<int32>(NewFrameRate); FR->Denominator = 1; Seq->MarkPackageDirty(); return true; }
			}
		}
	}
	// 回退：修改 SequenceLength 按旧帧率比例缩放
	{
		const float OldLen = Seq->GetPlayLength();
		if (OldLen <= 0.f) return false;
		FProperty* LenProp = FindFProperty<FProperty>(Seq->GetClass(), TEXT("SequenceLength"));
		if (FFloatProperty* FP = CastField<FFloatProperty>(LenProp))
		{
			// 按 NumFrames 推算旧帧率，按新帧率重算 SequenceLength
#if NX_UE_HAS_ANIM_SEQUENCE_SAMPLING_API
			const int32 NumFrames = Seq->GetNumberOfSampledKeys();
#elif WITH_EDITOR
			const int32 NumFrames = Seq->GetNumberOfFrames();
#else
			const int32 NumFrames = 0;
#endif
			if (NumFrames > 0)
			{
				const float NewLen = static_cast<float>(NumFrames) / NewFrameRate;
				*FP->ContainerPtrToValuePtr<float>(Seq) = NewLen;
				Seq->MarkPackageDirty();
				return true;
			}
		}
	}
	return false;
#endif
}

// ── 跨版本根运动模式设置辅助 ──────────────────────────────────────────────────
// UE4：bEnableRootMotionTranslation / bEnableRootMotionRotation（两个 bool）
// UE5：RootMotionMode（ERootMotionMode 枚举）

static bool SetAnimSequenceRootMotion(UAnimSequence* Seq, const FString& ModeStr, FString& OutModeName)
{
	if (!Seq) return false;

#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL || NX_UE_HAS_ANIM_SEQUENCE_ROOT_MOTION_MODE
	// UE5：使用 RootMotionMode 枚举
	ERootMotionMode::Type NewMode = ERootMotionMode::RootMotionFromMontagesOnly;
	if (ModeStr.Contains(TEXT("Everything")))
		NewMode = ERootMotionMode::RootMotionFromEverything;
	else if (ModeStr.Contains(TEXT("Montages")))
		NewMode = ERootMotionMode::RootMotionFromMontagesOnly;
	else if (ModeStr.Contains(TEXT("No")) || ModeStr.Contains(TEXT("Ignore")))
		NewMode = ERootMotionMode::NoRootMotionExtraction;

	// 通过反射写入（RootMotionMode 在某些版本是 protected）
	{
		FProperty* Prop = FindFProperty<FProperty>(Seq->GetClass(), TEXT("RootMotionMode"));
		if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			*BP->ContainerPtrToValuePtr<uint8>(Seq) = static_cast<uint8>(NewMode);
			Seq->MarkPackageDirty();
			OutModeName = (NewMode == ERootMotionMode::RootMotionFromEverything) ? TEXT("RootMotionFromEverything")
				: (NewMode == ERootMotionMode::RootMotionFromMontagesOnly) ? TEXT("RootMotionFromMontagesOnly")
				: TEXT("NoRootMotionExtraction");
			return true;
		}
		if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Seq), static_cast<int64>(NewMode));
			Seq->MarkPackageDirty();
			OutModeName = (NewMode == ERootMotionMode::RootMotionFromEverything) ? TEXT("RootMotionFromEverything")
				: (NewMode == ERootMotionMode::RootMotionFromMontagesOnly) ? TEXT("RootMotionFromMontagesOnly")
				: TEXT("NoRootMotionExtraction");
			return true;
		}
	}
	return false;
#else
	// UE4：bEnableRootMotionTranslation + bEnableRootMotionRotation
	const bool bEnable = !ModeStr.Contains(TEXT("No")) && !ModeStr.Contains(TEXT("Ignore"));
	const bool bTranslation = bEnable;
	const bool bRotation = bEnable && !ModeStr.Contains(TEXT("TranslationOnly"));

	if (FBoolProperty* TProp = CastField<FBoolProperty>(FindFProperty<FProperty>(Seq->GetClass(), TEXT("bEnableRootMotionTranslation"))))
		*TProp->ContainerPtrToValuePtr<bool>(Seq) = bTranslation;
	if (FBoolProperty* RProp = CastField<FBoolProperty>(FindFProperty<FProperty>(Seq->GetClass(), TEXT("bEnableRootMotionRotation"))))
		*RProp->ContainerPtrToValuePtr<bool>(Seq) = bRotation;

	Seq->MarkPackageDirty();
	OutModeName = bEnable ? TEXT("RootMotionFromEverything") : TEXT("NoRootMotionExtraction");
	return true;
#endif
}

// ── 骨骼曲线关键帧辅助（UE5.6+ IAnimationDataController；旧版走 RawCurveData）──────────

// 获取可写 FRawCurveTracks 指针（UE5.5+ RawCurveData 为 protected，通过反射访问）
static FRawCurveTracks* GetRawCurveDataPtr(UAnimSequenceBase* SeqBase)
{
	if (!SeqBase) return nullptr;
	FStructProperty* StructProp = FindFProperty<FStructProperty>(SeqBase->GetClass(), TEXT("RawCurveData"));
	if (!StructProp) return nullptr;
	return StructProp->ContainerPtrToValuePtr<FRawCurveTracks>(SeqBase);
}

// 跨版本获取曲线名
// UE 4.26–5.2：Name (FSmartName) 公开，取 Name.DisplayName
// UE 5.3+：CurveName 为 private，通过 GetName()/SetName() 访问
static FName GetFloatCurveName(const FFloatCurve& FC)
{
#if NX_UE_HAS_FLOAT_CURVE_SMART_NAME
	return FC.Name.DisplayName;
#else
	return FC.GetName();
#endif
}

static void SetFloatCurveName(FFloatCurve& FC, const FName& InName)
{
#if NX_UE_HAS_FLOAT_CURVE_SMART_NAME
	FC.Name.DisplayName = InName;
#else
	FC.SetName(InName);
#endif
}

void FManageAssetAnimSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_anim_sequence");
	Out.SearchAssetTypes = {TEXT("AnimSequence")};
	Out.Description = TEXT("Batch edit AnimSequence: notify/frame rate/root motion/curve keys.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Edit operation"),
			{ TEXT("add_notify"), TEXT("remove_notify"), TEXT("set_frame_rate"), TEXT("set_root_motion"),
			  TEXT("add_float_curve"), TEXT("set_curve_key"), TEXT("remove_curve") }))
		.Prop(TEXT("notifyName"),   FNexusSchema::Str(TEXT("Notify name (add/remove)")))
		.Prop(TEXT("notifyClass"),  FNexusSchema::Str(TEXT("Notify class path (add; default AnimNotify)")))
		.Prop(TEXT("notifyIndex"),  FNexusSchema::Int(TEXT("Notify index (remove)")))
		.Prop(TEXT("time"),         FNexusSchema::Num(TEXT("Trigger time sec (add_notify) or key time sec (set_curve_key)")))
		.Prop(TEXT("duration"),     FNexusSchema::Num(TEXT("Duration sec (State Notify > 0)")))
		.Prop(TEXT("frameRate"),    FNexusSchema::Num(TEXT("New frame rate (set_frame_rate)")))
		.Prop(TEXT("rootMotion"),   FNexusSchema::Str(TEXT("Root motion mode enum")))
		.Prop(TEXT("curveName"),    FNexusSchema::Str(TEXT("Curve name (add_float_curve/set_curve_key/remove_curve)")))
		.Prop(TEXT("value"),        FNexusSchema::Num(TEXT("Keyframe value (set_curve_key)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("AnimSequence asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch edit ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("notify"), TEXT("event"), TEXT("frame"), TEXT("fps"), TEXT("root motion"), TEXT("curve"), TEXT("keyframe") };
	Out.RelatedCapabilities = { TEXT("get_asset_anim_sequence"), TEXT("get_asset_anim_montage") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Add/remove AnimNotify, frame rate/root motion, float curves; persist with save_asset");
}

static UAnimSequence* SeqFrom(FNexusActionContext& Ctx)
{
	return static_cast<UAnimSequence*>(Ctx.Target);
}

static void HandleAnimSeq_AddNotify(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const FNexusArgs A(Op);
	const FString NotifyName = A.Str(TEXT("notifyName"));
	const FString NotifyClass = A.Str(TEXT("notifyClass"));
	const double Time = A.Num(TEXT("time"));
	const double Duration = A.Num(TEXT("duration"));
	if (NotifyName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_notify requires notifyName"));
		return;
	}

	const float PlayLength = Seq->GetPlayLength();
	const float TriggerTime = FMath::Clamp(static_cast<float>(Time), 0.f, PlayLength);

	if (Duration > 0.f)
	{
		UClass* StateClass = nullptr;
		if (!NotifyClass.IsEmpty())
		{
			StateClass = LoadObject<UClass>(nullptr, *NotifyClass);
		}
		if (!StateClass || !StateClass->IsChildOf(UAnimNotifyState::StaticClass()))
		{
			Ctx.Entry->SetStringField(TEXT("error"), TEXT("State Notify requires notifyClass derived from AnimNotifyState"));
			return;
		}
		const int32 NewIndex = Seq->Notifies.Add(FAnimNotifyEvent());
		FAnimNotifyEvent& NotifyEvent = Seq->Notifies[NewIndex];
		NotifyEvent.NotifyName = FName(*NotifyName);
		NotifyEvent.SetTime(TriggerTime);
		NotifyEvent.SetDuration(static_cast<float>(Duration));
		UAnimNotifyState* NotifyState = NewObject<UAnimNotifyState>(Seq, StateClass);
		NotifyEvent.NotifyStateClass = NotifyState;
		NotifyEvent.Notify = nullptr;
	}
	else
	{
		UClass* NotifyCls = UAnimNotify::StaticClass();
		if (!NotifyClass.IsEmpty())
		{
			if (UClass* CustomCls = LoadObject<UClass>(nullptr, *NotifyClass))
			{
				if (CustomCls->IsChildOf(UAnimNotify::StaticClass()))
					NotifyCls = CustomCls;
			}
		}
		const int32 NewIndex = Seq->Notifies.Add(FAnimNotifyEvent());
		FAnimNotifyEvent& NotifyEvent = Seq->Notifies[NewIndex];
		NotifyEvent.NotifyName = FName(*NotifyName);
		NotifyEvent.SetTime(TriggerTime);
		NotifyEvent.SetDuration(0.f);
		UAnimNotify* NotifyObj = NewObject<UAnimNotify>(Seq, NotifyCls);
		NotifyEvent.Notify = NotifyObj;
		NotifyEvent.NotifyStateClass = nullptr;
	}

	Seq->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("notifyName"), NotifyName);
	Ctx.Entry->SetNumberField(TEXT("time"), TriggerTime);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleAnimSeq_RemoveNotify(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const FNexusArgs A(Op);
	const FString NotifyName = A.Str(TEXT("notifyName"));
	int32 NotifyIndex = -1;
	if (Op->HasField(TEXT("notifyIndex")))
	{
		NotifyIndex = static_cast<int32>(A.Num(TEXT("notifyIndex"), -1.0));
	}
	if (NotifyIndex < 0 && NotifyName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_notify requires notifyIndex or notifyName"));
		return;
	}

	int32 TargetIndex = NotifyIndex;
	if (TargetIndex < 0 && !NotifyName.IsEmpty())
	{
		const FName TargetName(*NotifyName);
		for (int32 i = 0; i < Seq->Notifies.Num(); ++i)
		{
			if (Seq->Notifies[i].NotifyName == TargetName)
			{
				TargetIndex = i;
				break;
			}
		}
	}

	if (!Seq->Notifies.IsValidIndex(TargetIndex))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Invalid notify index/name"));
		return;
	}

	Seq->Notifies.RemoveAt(TargetIndex);
	Seq->MarkPackageDirty();
	Ctx.Entry->SetNumberField(TEXT("removedIndex"), TargetIndex);
	Ctx.Entry->SetBoolField(TEXT("removed"), true);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleAnimSeq_SetFrameRate(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const double FrameRate = FNexusArgs(Op).Num(TEXT("frameRate"));
	if (FrameRate <= 0)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_frame_rate requires frameRate > 0"));
		return;
	}
	if (!SetAnimSequenceFrameRate(Seq, static_cast<float>(FrameRate)))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Frame rate set failed (cross-version API unsupported)"));
		return;
	}
	Ctx.Entry->SetNumberField(TEXT("frameRate"), FrameRate);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleAnimSeq_SetRootMotion(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const FString RootMotion = FNexusArgs(Op).Str(TEXT("rootMotion"));
	if (RootMotion.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_root_motion requires rootMotion mode name"));
		return;
	}
	FString ModeName;
	if (!SetAnimSequenceRootMotion(Seq, RootMotion, ModeName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Root motion mode set failed"));
		return;
	}
	Ctx.Entry->SetStringField(TEXT("rootMotion"), ModeName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleAnimSeq_AddFloatCurve(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const FString CurveName = FNexusArgs(Op).Str(TEXT("curveName"));
	if (CurveName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_float_curve requires curveName"));
		return;
	}
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
	IAnimationDataController& Controller = Seq->GetController();
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	Controller.AddCurve(CurveId);
	Seq->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("curveName"), CurveName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	FRawCurveTracks* Curves = GetRawCurveDataPtr(Seq);
	if (!Curves)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Reflection failed to get RawCurveData"));
		return;
	}
	const FName CN(*CurveName);
	bool bAlreadyExists = false;
	for (const FFloatCurve& FC : Curves->FloatCurves)
		if (GetFloatCurveName(FC) == CN) { bAlreadyExists = true; break; }
	if (bAlreadyExists)
	{
		Ctx.Entry->SetStringField(TEXT("note"), FString::Printf(TEXT("Curve already exists: %s"), *CurveName));
	}
	else
	{
		FFloatCurve NewCurve;
		SetFloatCurveName(NewCurve, CN);
		Curves->FloatCurves.Add(NewCurve);
		Seq->MarkPackageDirty();
		Ctx.Entry->SetStringField(TEXT("curveName"), CurveName);
		Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
	}
#endif
}

static void HandleAnimSeq_SetCurveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const FNexusArgs A(Op);
	const FString CurveName = A.Str(TEXT("curveName"));
	const double Time = A.Num(TEXT("time"));
	const double KeyValue = A.Num(TEXT("value"));
	if (CurveName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_curve_key requires curveName"));
		return;
	}
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
	IAnimationDataController& Controller = Seq->GetController();
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	Controller.SetCurveKey(CurveId, FRichCurveKey(static_cast<float>(Time), static_cast<float>(KeyValue)));
	Seq->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("curveName"), CurveName);
	Ctx.Entry->SetNumberField(TEXT("time"),      Time);
	Ctx.Entry->SetNumberField(TEXT("value"),     KeyValue);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	FRawCurveTracks* Curves = GetRawCurveDataPtr(Seq);
	if (!Curves)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Reflection failed to get RawCurveData"));
		return;
	}
	const FName CN(*CurveName);
	FFloatCurve* FC = nullptr;
	for (FFloatCurve& C : Curves->FloatCurves)
		if (GetFloatCurveName(C) == CN) { FC = &C; break; }
	if (!FC)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Curve not found: %s; use add_float_curve first"), *CurveName));
		return;
	}
	FC->FloatCurve.AddKey(static_cast<float>(Time), static_cast<float>(KeyValue));
	Seq->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("curveName"), CurveName);
	Ctx.Entry->SetNumberField(TEXT("time"),      Time);
	Ctx.Entry->SetNumberField(TEXT("value"),     KeyValue);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#endif
}

static void HandleAnimSeq_RemoveCurve(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimSequence* Seq = SeqFrom(Ctx);
	const FString CurveName = FNexusArgs(Op).Str(TEXT("curveName"));
	if (CurveName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_curve requires curveName"));
		return;
	}
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
	IAnimationDataController& Controller = Seq->GetController();
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	Controller.RemoveCurve(CurveId);
	Seq->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("curveName"), CurveName);
	Ctx.Entry->SetBoolField(TEXT("removed"), true);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	FRawCurveTracks* Curves = GetRawCurveDataPtr(Seq);
	if (!Curves)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Reflection failed to get RawCurveData"));
		return;
	}
	const FName CN(*CurveName);
	int32 RemoveIdx = INDEX_NONE;
	for (int32 i = 0; i < Curves->FloatCurves.Num(); ++i)
		if (GetFloatCurveName(Curves->FloatCurves[i]) == CN) { RemoveIdx = i; break; }
	if (RemoveIdx == INDEX_NONE)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Curve not found: %s"), *CurveName));
		return;
	}
	Curves->FloatCurves.RemoveAt(RemoveIdx);
	Seq->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("curveName"), CurveName);
	Ctx.Entry->SetBoolField(TEXT("removed"), true);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#endif
}

bool FManageAssetAnimSequenceCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UAnimSequence* Seq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(AssetPath);
	if (!Seq)
	{
		OutError = FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Seq;
	return true;
}

void FManageAssetAnimSequenceCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_notify"),      &HandleAnimSeq_AddNotify);
	OutHandlers.Add(TEXT("remove_notify"),   &HandleAnimSeq_RemoveNotify);
	OutHandlers.Add(TEXT("set_frame_rate"),  &HandleAnimSeq_SetFrameRate);
	OutHandlers.Add(TEXT("set_root_motion"), &HandleAnimSeq_SetRootMotion);
	OutHandlers.Add(TEXT("add_float_curve"), &HandleAnimSeq_AddFloatCurve);
	OutHandlers.Add(TEXT("set_curve_key"),   &HandleAnimSeq_SetCurveKey);
	OutHandlers.Add(TEXT("remove_curve"),    &HandleAnimSeq_RemoveCurve);
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimSequenceCapability)
