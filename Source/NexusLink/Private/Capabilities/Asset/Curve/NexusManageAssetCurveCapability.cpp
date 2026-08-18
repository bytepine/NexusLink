// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Curve/NexusManageAssetCurveCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Curves/RealCurve.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/CurveTable.h"
#include "NexusMcpTool.h"

static ERichCurveInterpMode InterpFromStr(const FString& S)
{
	if (S.Equals(TEXT("constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
	if (S.Equals(TEXT("linear"), ESearchCase::IgnoreCase))   return RCIM_Linear;
	return RCIM_Cubic;
}

/** 根据 channel 名称从 UCurveBase 获取 FRichCurve 指针（CurveTable 须另行处理）。 */
static FRichCurve* GetChannel(UCurveBase* CB, const FString& Channel)
{
	if (UCurveFloat* CF = Cast<UCurveFloat>(CB))
		return &CF->FloatCurve;

	if (UCurveVector* CV = Cast<UCurveVector>(CB))
	{
		if (Channel == TEXT("X") || Channel == TEXT("0")) return &CV->FloatCurves[0];
		if (Channel == TEXT("Y") || Channel == TEXT("1")) return &CV->FloatCurves[1];
		if (Channel == TEXT("Z") || Channel == TEXT("2")) return &CV->FloatCurves[2];
	}
	if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(CB))
	{
		if (Channel == TEXT("R") || Channel == TEXT("0")) return &CC->FloatCurves[0];
		if (Channel == TEXT("G") || Channel == TEXT("1")) return &CC->FloatCurves[1];
		if (Channel == TEXT("B") || Channel == TEXT("2")) return &CC->FloatCurves[2];
		if (Channel == TEXT("A") || Channel == TEXT("3")) return &CC->FloatCurves[3];
	}
	return nullptr;
}

/** 从 CurveTable 按行名获取可写 FRichCurve（行须已存在）。默认 RichCurves 模式下 static_cast 安全。 */
static FRichCurve* GetTableRow(UCurveTable* CT, const FName& RowName)
{
#if NX_UE_HAS_CURVE_TABLE_FIND_UNCHECKED
	// UE5: FindCurveUnchecked(FName) 只有1个参数
	FRealCurve* Found = CT->FindCurveUnchecked(RowName);
#else
	// UE4: FindCurve(FName, FString, bool) 返回 FRealCurve*（UE4.25+）
	FRealCurve* Found = CT->FindCurve(RowName, FString(TEXT("NexusLink")), false);
#endif
	return Found ? static_cast<FRichCurve*>(Found) : nullptr;
}

void FManageAssetCurveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_curve");
	Out.SearchAssetTypes = {TEXT("CurveFloat"), TEXT("CurveVector"), TEXT("CurveLinearColor"), TEXT("CurveTable")};
	Out.Description = TEXT("Edit curve keyframes. CurveTable uses rowName instead of channel.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"),  FNexusSchema::Str(TEXT("add_key / set_key / remove_key / set_interp")))
		.Prop(TEXT("channel"),     FNexusSchema::Str(TEXT("CurveFloat: 'Value'；Vector: X/Y/Z；Color: R/G/B/A")))
		.Prop(TEXT("rowName"),     FNexusSchema::Str(TEXT("CurveTable row name")))
		.Prop(TEXT("time"),        FNexusSchema::Num(TEXT("Keyframe time")))
		.Prop(TEXT("value"),       FNexusSchema::Num(TEXT("Keyframe value")))
		.Prop(TEXT("interp"),      FNexusSchema::Str(TEXT("cubic (default)/ linear / constant")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("Asset package path")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("curve"), TEXT("keyframe"), TEXT("timeline"), TEXT("interp"), TEXT("add"), TEXT("remove") };
	Out.RelatedCapabilities = { TEXT("create_asset_curve"), TEXT("get_asset_curve") };
}

struct FCurveActionState
{
	UObject* AssetObj = nullptr;
	UCurveBase* CB = nullptr;
	UCurveTable* CT = nullptr;
};

static FCurveActionState* CurveState(FNexusActionContext& Ctx)
{
	return static_cast<FCurveActionState*>(Ctx.Target);
}

static FRichCurve* ResolveCurve(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FCurveActionState* S = CurveState(Ctx);
	const FNexusArgs A(Op);
	const FString Channel = A.Str(TEXT("channel"), TEXT("Value"));
	const FString RowName = A.Str(TEXT("rowName"));
	FRichCurve* Curve = S->CT
		? GetTableRow(S->CT, FName(*RowName))
		: GetChannel(S->CB, Channel);
	if (!Curve)
	{
		const FString ErrCtx = S->CT ? RowName : Channel;
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Channel/row '%s' not found (action=%s)"), *ErrCtx, *Ctx.Action));
	}
	return Curve;
}

static void HandleCurve_AddKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const FNexusArgs A(Op);
	const float Time  = static_cast<float>(A.Num(TEXT("time"), 0.f));
	const float Value = static_cast<float>(A.Num(TEXT("value"), 0.f));
	const FString Interp = A.Str(TEXT("interp"), TEXT("cubic"));
	const FKeyHandle Handle = Curve->AddKey(Time, Value);
	Curve->SetKeyInterpMode(Handle, InterpFromStr(Interp));
	Ctx.Entry->SetNumberField(TEXT("time"), Time);
	Ctx.Entry->SetNumberField(TEXT("value"), Value);
}

static void HandleCurve_SetKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const FNexusArgs A(Op);
	const float Time = static_cast<float>(A.Num(TEXT("time"), 0.f));
	const FKeyHandle Handle = Curve->FindKey(Time);
	if (Handle == FKeyHandle::Invalid())
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("No keyframe at time %.4f (set_key)"), Time));
		return;
	}
	if (Op->HasField(TEXT("value")))
		Curve->SetKeyValue(Handle, static_cast<float>(A.Num(TEXT("value"), 0.f)));
	if (Op->HasField(TEXT("interp")))
		Curve->SetKeyInterpMode(Handle, InterpFromStr(A.Str(TEXT("interp"), TEXT("cubic"))));
}

static void HandleCurve_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const float Time = static_cast<float>(FNexusArgs(Op).Num(TEXT("time"), 0.f));
	const FKeyHandle Handle = Curve->FindKey(Time);
	if (Handle == FKeyHandle::Invalid())
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("No keyframe at time %.4f (remove_key)"), Time));
		return;
	}
	Curve->DeleteKey(Handle);
}

static void HandleCurve_SetInterp(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const FNexusArgs A(Op);
	const FString Interp = A.Str(TEXT("interp"), TEXT("cubic"));
	if (Op->HasField(TEXT("time")))
	{
		const float Time = static_cast<float>(A.Num(TEXT("time"), 0.f));
		const FKeyHandle Handle = Curve->FindKey(Time);
		if (Handle != FKeyHandle::Invalid())
			Curve->SetKeyInterpMode(Handle, InterpFromStr(Interp));
	}
	else
	{
		for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
			Curve->SetKeyInterpMode(*It, InterpFromStr(Interp));
	}
}

bool FManageAssetCurveCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UObject* AssetObj = LoadObject<UObject>(nullptr, *AssetPath);
	if (!AssetObj)
	{
		OutError = FString::Printf(TEXT("Failed to load curve asset: %s"), *AssetPath);
		return false;
	}
	UCurveBase* CB = Cast<UCurveBase>(AssetObj);
	UCurveTable* CT = Cast<UCurveTable>(AssetObj);
	if (!CB && !CT)
	{
		OutError = FString::Printf(TEXT("Asset is not a curve type: %s"), *AssetPath);
		return false;
	}
	FCurveActionState* State = new FCurveActionState();
	State->AssetObj = AssetObj;
	State->CB = CB;
	State->CT = CT;
	OutTarget = State;
	return true;
}

void FManageAssetCurveCapability::FinalizeTarget(void* Target) const
{
	FCurveActionState* State = static_cast<FCurveActionState*>(Target);
	if (!State) return;
	if (State->AssetObj)
	{
		State->AssetObj->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetCurveCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_key"),    &HandleCurve_AddKey);
	OutHandlers.Add(TEXT("set_key"),    &HandleCurve_SetKey);
	OutHandlers.Add(TEXT("remove_key"), &HandleCurve_RemoveKey);
	OutHandlers.Add(TEXT("set_interp"), &HandleCurve_SetInterp);
}

REGISTER_MCP_CAPABILITY(FManageAssetCurveCapability)
