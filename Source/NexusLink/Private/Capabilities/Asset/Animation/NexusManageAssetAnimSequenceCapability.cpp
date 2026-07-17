// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("批量编辑 AnimSequence：notify/帧率/root motion/曲线关键帧。见 operations[].action。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("编辑操作"),
			{ TEXT("add_notify"), TEXT("remove_notify"), TEXT("set_frame_rate"), TEXT("set_root_motion"),
			  TEXT("add_float_curve"), TEXT("set_curve_key"), TEXT("remove_curve") }))
		.Prop(TEXT("notifyName"),   FNexusSchema::Str(TEXT("Notify 名（add/remove）")))
		.Prop(TEXT("notifyClass"),  FNexusSchema::Str(TEXT("Notify 类路径（add；默认 AnimNotify）")))
		.Prop(TEXT("notifyIndex"),  FNexusSchema::Int(TEXT("Notify 索引（remove）")))
		.Prop(TEXT("time"),         FNexusSchema::Num(TEXT("触发时间秒（add_notify）或关键帧时间秒（set_curve_key）")))
		.Prop(TEXT("duration"),     FNexusSchema::Num(TEXT("持续秒（State Notify 时 > 0）")))
		.Prop(TEXT("frameRate"),    FNexusSchema::Num(TEXT("新帧率（set_frame_rate）")))
		.Prop(TEXT("rootMotion"),   FNexusSchema::Str(TEXT("根运动模式：RootMotionFromEverything|RootMotionFromMontagesOnly|NoRootMotionExtraction")))
		.Prop(TEXT("curveName"),    FNexusSchema::Str(TEXT("曲线名（add_float_curve / set_curve_key / remove_curve）")))
		.Prop(TEXT("value"),        FNexusSchema::Num(TEXT("关键帧值（set_curve_key）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("AnimSequence 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量编辑操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("notify"), TEXT("event"), TEXT("frame"), TEXT("fps"), TEXT("root motion"), TEXT("curve"), TEXT("keyframe") };
	Out.RelatedCapabilities = { TEXT("get_asset_anim_sequence"), TEXT("get_asset_anim_montage") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("增删 AnimNotify、改帧率/根运动、管理浮点曲线；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetAnimSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UAnimSequence* Seq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(AssetPath);
		if (!Seq)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("AnimSequence 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr; // NOLINT(bugprone-argument-comment) 复用下方原有单操作逻辑，读入范围收窄为当前 op

		FString Action;
		OpArgs->TryGetStringField(TEXT("action"), Action);

		FString NotifyName, NotifyClass, RootMotion;
		double Time = 0.0, Duration = 0.0, FrameRate = 0.0;
		int32 NotifyIndex = -1;
		if (OpArgs.IsValid())
		{
			OpArgs->TryGetStringField(TEXT("notifyName"),  NotifyName);
			OpArgs->TryGetStringField(TEXT("notifyClass"), NotifyClass);
			OpArgs->TryGetStringField(TEXT("rootMotion"),  RootMotion);
			OpArgs->TryGetNumberField(TEXT("time"),         Time);
			OpArgs->TryGetNumberField(TEXT("duration"),     Duration);
			OpArgs->TryGetNumberField(TEXT("frameRate"),    FrameRate);
			if (OpArgs->HasField(TEXT("notifyIndex")))
				NotifyIndex = static_cast<int32>(OpArgs->GetNumberField(TEXT("notifyIndex")));
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("add_notify"), ESearchCase::IgnoreCase))
		{
			if (NotifyName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_notify 需要 notifyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			const float PlayLength = Seq->GetPlayLength();
			const float TriggerTime = FMath::Clamp(static_cast<float>(Time), 0.f, PlayLength);

			if (Duration > 0.f)
			{
				// 添加 AnimNotifyState（需指定类）
				UClass* StateClass = nullptr;
				if (!NotifyClass.IsEmpty())
				{
					StateClass = LoadObject<UClass>(nullptr, *NotifyClass);
				}
				if (!StateClass || !StateClass->IsChildOf(UAnimNotifyState::StaticClass()))
				{
					Entry->SetStringField(TEXT("error"), TEXT("State Notify 需要 notifyClass 继承自 AnimNotifyState"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				int32 NewIndex = Seq->Notifies.Add(FAnimNotifyEvent());
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
				// 添加 AnimNotify（瞬发）
				UClass* NotifyCls = UAnimNotify::StaticClass();
				if (!NotifyClass.IsEmpty())
				{
					if (UClass* CustomCls = LoadObject<UClass>(nullptr, *NotifyClass))
					{
						if (CustomCls->IsChildOf(UAnimNotify::StaticClass()))
							NotifyCls = CustomCls;
					}
				}
				int32 NewIndex = Seq->Notifies.Add(FAnimNotifyEvent());
				FAnimNotifyEvent& NotifyEvent = Seq->Notifies[NewIndex];
				NotifyEvent.NotifyName = FName(*NotifyName);
				NotifyEvent.SetTime(TriggerTime);
				NotifyEvent.SetDuration(0.f);
				UAnimNotify* NotifyObj = NewObject<UAnimNotify>(Seq, NotifyCls);
				NotifyEvent.Notify = NotifyObj;
				NotifyEvent.NotifyStateClass = nullptr;
			}

			Seq->MarkPackageDirty();
			Entry->SetStringField(TEXT("notifyName"), NotifyName);
			Entry->SetNumberField(TEXT("time"), TriggerTime);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("remove_notify"), ESearchCase::IgnoreCase))
		{
			if (NotifyIndex < 0 && NotifyName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_notify 需要 notifyIndex 或 notifyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
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
				Entry->SetStringField(TEXT("error"), TEXT("Notify 索引/名称无效"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Seq->Notifies.RemoveAt(TargetIndex);
			Seq->MarkPackageDirty();
			Entry->SetNumberField(TEXT("removedIndex"), TargetIndex);
			Entry->SetBoolField(TEXT("removed"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("set_frame_rate"), ESearchCase::IgnoreCase))
		{
			if (FrameRate <= 0)
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_frame_rate 需要 frameRate > 0"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			if (!SetAnimSequenceFrameRate(Seq, static_cast<float>(FrameRate)))
			{
				Entry->SetStringField(TEXT("error"), TEXT("帧率设置失败（跨版本 API 不支持）"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetNumberField(TEXT("frameRate"), FrameRate);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("set_root_motion"), ESearchCase::IgnoreCase))
		{
			if (RootMotion.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_root_motion 需要 rootMotion 模式名"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString ModeName;
			if (!SetAnimSequenceRootMotion(Seq, RootMotion, ModeName))
			{
				Entry->SetStringField(TEXT("error"), TEXT("根运动模式设置失败"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetStringField(TEXT("rootMotion"), ModeName);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("add_float_curve"), ESearchCase::IgnoreCase))
		{
			FString CurveName;
			if (!OpArgs.IsValid() || !OpArgs->TryGetStringField(TEXT("curveName"), CurveName) || CurveName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_float_curve 需要 curveName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
			IAnimationDataController& Controller = Seq->GetController();
			const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
			Controller.AddCurve(CurveId);
			Seq->MarkPackageDirty();
			Entry->SetStringField(TEXT("curveName"), CurveName);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#else
			// UE4/UE5早期：通过反射访问 RawCurveData（UE5.5+ 为 protected）
			FRawCurveTracks* Curves = GetRawCurveDataPtr(Seq);
			if (!Curves)
			{
				Entry->SetStringField(TEXT("error"), TEXT("反射获取 RawCurveData 失败"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const FName CN(*CurveName);
			bool bAlreadyExists = false;
			for (const FFloatCurve& FC : Curves->FloatCurves)
				if (GetFloatCurveName(FC) == CN) { bAlreadyExists = true; break; }
			if (bAlreadyExists)
			{
				Entry->SetStringField(TEXT("note"), FString::Printf(TEXT("曲线已存在: %s"), *CurveName));
			}
			else
			{
				FFloatCurve NewCurve;
				SetFloatCurveName(NewCurve, CN);
				Curves->FloatCurves.Add(NewCurve);
				Seq->MarkPackageDirty();
				Entry->SetStringField(TEXT("curveName"), CurveName);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
#endif
		}
		else if (Action.Equals(TEXT("set_curve_key"), ESearchCase::IgnoreCase))
		{
			FString CurveName;
			if (!OpArgs.IsValid() || !OpArgs->TryGetStringField(TEXT("curveName"), CurveName) || CurveName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_curve_key 需要 curveName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
			{
				double KeyValue = 0.0;
				if (OpArgs.IsValid()) OpArgs->TryGetNumberField(TEXT("value"), KeyValue);
				IAnimationDataController& Controller = Seq->GetController();
				const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
				Controller.SetCurveKey(CurveId, FRichCurveKey(static_cast<float>(Time), static_cast<float>(KeyValue)));
				Seq->MarkPackageDirty();
				Entry->SetStringField(TEXT("curveName"), CurveName);
				Entry->SetNumberField(TEXT("time"),      Time);
				Entry->SetNumberField(TEXT("value"),     KeyValue);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
#else
			double KeyValue = 0.0;
			if (OpArgs.IsValid()) OpArgs->TryGetNumberField(TEXT("value"), KeyValue);
			FRawCurveTracks* Curves = GetRawCurveDataPtr(Seq);
			if (!Curves)
			{
				Entry->SetStringField(TEXT("error"), TEXT("反射获取 RawCurveData 失败"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const FName CN(*CurveName);
			FFloatCurve* FC = nullptr;
			for (FFloatCurve& C : Curves->FloatCurves)
				if (GetFloatCurveName(C) == CN) { FC = &C; break; }
			if (!FC)
			{
				Entry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("曲线未找到: %s；先用 add_float_curve 创建"), *CurveName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FC->FloatCurve.AddKey(static_cast<float>(Time), static_cast<float>(KeyValue));
			Seq->MarkPackageDirty();
			Entry->SetStringField(TEXT("curveName"), CurveName);
			Entry->SetNumberField(TEXT("time"),      Time);
			Entry->SetNumberField(TEXT("value"),     KeyValue);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#endif
		}
		else if (Action.Equals(TEXT("remove_curve"), ESearchCase::IgnoreCase))
		{
			FString CurveName;
			if (!OpArgs.IsValid() || !OpArgs->TryGetStringField(TEXT("curveName"), CurveName) || CurveName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_curve 需要 curveName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
			IAnimationDataController& Controller = Seq->GetController();
			const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
			Controller.RemoveCurve(CurveId);
			Seq->MarkPackageDirty();
			Entry->SetStringField(TEXT("curveName"), CurveName);
			Entry->SetBoolField(TEXT("removed"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#else
			FRawCurveTracks* Curves = GetRawCurveDataPtr(Seq);
			if (!Curves)
			{
				Entry->SetStringField(TEXT("error"), TEXT("反射获取 RawCurveData 失败"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const FName CN(*CurveName);
			int32 RemoveIdx = INDEX_NONE;
			for (int32 i = 0; i < Curves->FloatCurves.Num(); ++i)
				if (GetFloatCurveName(Curves->FloatCurves[i]) == CN) { RemoveIdx = i; break; }
			if (RemoveIdx == INDEX_NONE)
			{
				Entry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("曲线未找到: %s"), *CurveName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Curves->FloatCurves.RemoveAt(RemoveIdx);
			Seq->MarkPackageDirty();
			Entry->SetStringField(TEXT("curveName"), CurveName);
			Entry->SetBoolField(TEXT("removed"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#endif
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimSequenceCapability)
