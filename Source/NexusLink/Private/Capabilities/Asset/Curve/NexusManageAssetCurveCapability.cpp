// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Curve/NexusManageAssetCurveCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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

FCapabilityResult FManageAssetCurveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		// UCurveTable 不继承自 UCurveBase，加载为 UObject* 再分别 Cast 以避免类型不相关警告
		UObject* AssetObj = LoadObject<UObject>(nullptr, *AssetPath);
		if (!AssetObj)
		{
			OutError = FString::Printf(TEXT("Failed to load curve asset: %s"), *AssetPath);
			return;
		}

		UCurveBase* CB = Cast<UCurveBase>(AssetObj);
		UCurveTable* CT = Cast<UCurveTable>(AssetObj);

		if (!CB && !CT)
		{
			OutError = FString::Printf(TEXT("Asset is not a curve type: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>& Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			const FString Action  = FNexusArgs(Op).Str(TEXT("action"));
			const FString Channel = FNexusArgs(Op).Str(TEXT("channel"), TEXT("Value"));
			const FString RowName = FNexusArgs(Op).Str(TEXT("rowName"));
			const float   Time    =FNexusArgs(Op).Num(TEXT("time"), 0.f);
			const float   Value   =FNexusArgs(Op).Num(TEXT("value"), 0.f);
			const FString Interp  = FNexusArgs(Op).Str(TEXT("interp"), TEXT("cubic"));

			FRichCurve* Curve = CT
				? GetTableRow(CT, FName(*RowName))
				: GetChannel(CB, Channel);

			if (!Curve)
			{
				const FString ErrCtx = CT ? RowName : Channel;
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("Channel/row '%s' not found (action=%s)"), *ErrCtx, *Action));
				continue;
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_key"))
			{
				const FKeyHandle Handle = Curve->AddKey(Time, Value);
				Curve->SetKeyInterpMode(Handle, InterpFromStr(Interp));
				Entry->SetNumberField(TEXT("time"), Time);
				Entry->SetNumberField(TEXT("value"), Value);
			}
			else if (Action == TEXT("set_key"))
			{
				const FKeyHandle Handle = Curve->FindKey(Time);
				if (Handle == FKeyHandle::Invalid())
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
						FString::Printf(TEXT("No keyframe at time %.4f (set_key)"), Time));
					continue;
				}
				if (Op->HasField(TEXT("value")))  Curve->SetKeyValue(Handle, Value);
				if (Op->HasField(TEXT("interp")))  Curve->SetKeyInterpMode(Handle, InterpFromStr(Interp));
			}
			else if (Action == TEXT("remove_key"))
			{
				const FKeyHandle Handle = Curve->FindKey(Time);
				if (Handle == FKeyHandle::Invalid())
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
						FString::Printf(TEXT("No keyframe at time %.4f (remove_key)"), Time));
					continue;
				}
				Curve->DeleteKey(Handle);
			}
			else if (Action == TEXT("set_interp"))
			{
				if (Op->HasField(TEXT("time")))
				{
					const FKeyHandle Handle = Curve->FindKey(Time);
					if (Handle != FKeyHandle::Invalid())
						Curve->SetKeyInterpMode(Handle, InterpFromStr(Interp));
				}
				else
				{
					// 对所有关键帧设置插值
					for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
						Curve->SetKeyInterpMode(*It, InterpFromStr(Interp));
				}
			}
			else
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("Unknown action: %s"), *Action));
				continue;
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		AssetObj->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetCurveCapability)
