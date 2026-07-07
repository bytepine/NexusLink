// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Curve/NexusGetAssetCurveCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Curves/RealCurve.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/CurveTable.h"
#include "NexusMcpTool.h"

namespace NexusCurveUtils
{
	static FString InterpToStr(ERichCurveInterpMode M)
	{
		switch (M)
		{
			case RCIM_Constant: return TEXT("constant");
			case RCIM_Linear:   return TEXT("linear");
			case RCIM_Cubic:    return TEXT("cubic");
			default:            return TEXT("none");
		}
	}

	static TSharedPtr<FJsonObject> SerializeChannel(const FString& ChannelName, const FRichCurve& Curve)
	{
		TSharedPtr<FJsonObject> Chan = MakeShared<FJsonObject>();
		Chan->SetStringField(TEXT("name"), ChannelName);
		Chan->SetNumberField(TEXT("keyCount"), Curve.Keys.Num());

		TArray<TSharedPtr<FJsonValue>> KeysArr;
		for (const FRichCurveKey& Key : Curve.Keys)
		{
			TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
			K->SetNumberField(TEXT("time"),   Key.Time);
			K->SetNumberField(TEXT("value"),  Key.Value);
			K->SetStringField(TEXT("interp"), InterpToStr(Key.InterpMode));
			KeysArr.Add(MakeShared<FJsonValueObject>(K));
		}
		Chan->SetArrayField(TEXT("keys"), KeysArr);
		return Chan;
	}
}

void FGetAssetCurveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_curve");
	Out.Description = TEXT("读取曲线资产（CurveFloat/Vector/LinearColor/CurveTable）的通道与关键帧。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("curve"), TEXT("keyframe"), TEXT("channel"), TEXT("timeline"), TEXT("read") };
	Out.RelatedCapabilities = { TEXT("create_asset_curve"), TEXT("manage_asset_curve") };
}

FCapabilityResult FGetAssetCurveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		// UCurveTable 不继承自 UCurveBase，需分开加载以避免 Cast<> 警告
		UObject* AssetObj = LoadObject<UObject>(nullptr, *AssetPath);
		if (!AssetObj)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载曲线资产失败: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), AssetObj->GetName());
		Entry->SetStringField(TEXT("path"), AssetObj->GetPathName());

		if (UCurveFloat* CF = Cast<UCurveFloat>(AssetObj))
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("CurveFloat"));
			TArray<TSharedPtr<FJsonValue>> ChansArr;
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("Value"), CF->FloatCurve)));
			Entry->SetArrayField(TEXT("channels"), ChansArr);
		}
		else if (UCurveVector* CV = Cast<UCurveVector>(AssetObj))
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("CurveVector"));
			TArray<TSharedPtr<FJsonValue>> ChansArr;
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("X"), CV->FloatCurves[0])));
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("Y"), CV->FloatCurves[1])));
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("Z"), CV->FloatCurves[2])));
			Entry->SetArrayField(TEXT("channels"), ChansArr);
		}
		else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(AssetObj))
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("CurveLinearColor"));
			TArray<TSharedPtr<FJsonValue>> ChansArr;
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("R"), CC->FloatCurves[0])));
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("G"), CC->FloatCurves[1])));
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("B"), CC->FloatCurves[2])));
			ChansArr.Add(MakeShared<FJsonValueObject>(NexusCurveUtils::SerializeChannel(TEXT("A"), CC->FloatCurves[3])));
			Entry->SetArrayField(TEXT("channels"), ChansArr);
		}
		else if (UCurveTable* CT = Cast<UCurveTable>(AssetObj))
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("CurveTable"));
			TArray<TSharedPtr<FJsonValue>> RowsArr;
			// UE4.25+: GetRowMap() 返回 TMap<FName, FRealCurve*>；默认 RichCurves 模式下 static_cast 安全
			for (const auto& Pair : CT->GetRowMap())
			{
				FRichCurve* RC = static_cast<FRichCurve*>(Pair.Value);
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Pair.Key.ToString());
				Row->SetNumberField(TEXT("keyCount"), RC ? RC->Keys.Num() : 0);
				RowsArr.Add(MakeShared<FJsonValueObject>(Row));
			}
			Entry->SetArrayField(TEXT("rows"), RowsArr);
		}
		else
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("Unknown"));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetCurveCapability)
