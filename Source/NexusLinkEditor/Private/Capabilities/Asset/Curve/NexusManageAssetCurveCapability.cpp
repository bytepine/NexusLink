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

void FNexusManageAssetCurveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetCurve");
	Out.SearchAssetTypes = {TEXT("CurveFloat"), TEXT("CurveVector"), TEXT("CurveLinearColor"), TEXT("CurveTable")};
	Out.Description = TEXT("Edit curve keyframes. CurveTable uses RowName instead of channel.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"),  FNexusSchema::Str(TEXT("AddKey / SetKey / RemoveKey / SetInterp")))
		.Prop(TEXT("Channel"),     FNexusSchema::Str(TEXT("CurveFloat: 'Value'；Vector: X/Y/Z；Color: R/G/B/A")))
		.Prop(TEXT("RowName"),     FNexusSchema::Str(TEXT("CurveTable row name")))
		.Prop(TEXT("Time"),        FNexusSchema::Num(TEXT("Keyframe time")))
		.Prop(TEXT("Value"),       FNexusSchema::Num(TEXT("Keyframe value")))
		.Prop(TEXT("Interp"),      FNexusSchema::Str(TEXT("cubic (default)/ linear / constant")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("Asset package path")))
		.Required(TEXT("Operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("curve"), TEXT("keyframe"), TEXT("timeline"), TEXT("Interp"), TEXT("Add"), TEXT("Remove") };
	Out.RelatedCapabilities = { TEXT("CreateAssetCurve"), TEXT("GetAssetCurve") };
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
	const FString Channel = A.Str(TEXT("Channel"), TEXT("Value"));
	const FString RowName = A.Str(TEXT("RowName"));
	FRichCurve* Curve = S->CT
		? GetTableRow(S->CT, FName(*RowName))
		: GetChannel(S->CB, Channel);
	if (!Curve)
	{
		const FString ErrCtx = S->CT ? RowName : Channel;
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("Channel/row '%s' not found (Action=%s)"), *ErrCtx, *Ctx.Action));
	}
	return Curve;
}

static void HandleCurve_AddKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const FNexusArgs A(Op);
	const float Time  = static_cast<float>(A.Num(TEXT("Time"), 0.f));
	const float Value = static_cast<float>(A.Num(TEXT("Value"), 0.f));
	const FString Interp = A.Str(TEXT("Interp"), TEXT("cubic"));
	const FKeyHandle Handle = Curve->AddKey(Time, Value);
	Curve->SetKeyInterpMode(Handle, InterpFromStr(Interp));
	Ctx.Entry->SetNumberField(TEXT("Time"), Time);
	Ctx.Entry->SetNumberField(TEXT("Value"), Value);
}

static void HandleCurve_SetKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const FNexusArgs A(Op);
	const float Time = static_cast<float>(A.Num(TEXT("Time"), 0.f));
	const FKeyHandle Handle = Curve->FindKey(Time);
	if (Handle == FKeyHandle::Invalid())
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("No keyframe at time %.4f (SetKey)"), Time));
		return;
	}
	if (Op->HasField(TEXT("Value")))
		Curve->SetKeyValue(Handle, static_cast<float>(A.Num(TEXT("Value"), 0.f)));
	if (Op->HasField(TEXT("Interp")))
		Curve->SetKeyInterpMode(Handle, InterpFromStr(A.Str(TEXT("Interp"), TEXT("cubic"))));
}

static void HandleCurve_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const float Time = static_cast<float>(FNexusArgs(Op).Num(TEXT("Time"), 0.f));
	const FKeyHandle Handle = Curve->FindKey(Time);
	if (Handle == FKeyHandle::Invalid())
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("No keyframe at time %.4f (RemoveKey)"), Time));
		return;
	}
	Curve->DeleteKey(Handle);
}

static void HandleCurve_SetInterp(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FRichCurve* Curve = ResolveCurve(Op, Ctx);
	if (!Curve) return;
	const FNexusArgs A(Op);
	const FString Interp = A.Str(TEXT("Interp"), TEXT("cubic"));
	if (Op->HasField(TEXT("Time")))
	{
		const float Time = static_cast<float>(A.Num(TEXT("Time"), 0.f));
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

bool FNexusManageAssetCurveCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetCurveCapability::FinalizeTarget(void* Target) const
{
	FCurveActionState* State = static_cast<FCurveActionState*>(Target);
	if (!State) return;
	if (State->AssetObj)
	{
		State->AssetObj->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetCurveCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddKey"),    &HandleCurve_AddKey);
	OutHandlers.Add(TEXT("SetKey"),    &HandleCurve_SetKey);
	OutHandlers.Add(TEXT("RemoveKey"), &HandleCurve_RemoveKey);
	OutHandlers.Add(TEXT("SetInterp"), &HandleCurve_SetInterp);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetCurveCapability)
