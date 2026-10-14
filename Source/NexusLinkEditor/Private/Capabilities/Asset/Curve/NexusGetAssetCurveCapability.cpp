// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Curve/NexusGetAssetCurveCapability.h"
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

static FString InterpToStr(ERichCurveInterpMode M)
{
	switch (M)
	{
		case RCIM_Constant: return TEXT("constant");
		case RCIM_Linear:   return TEXT("linear");
		case RCIM_Cubic:    return TEXT("cubic");
		default:            return TEXT("None");
	}
}

static TSharedPtr<FJsonObject> SerializeChannel(const FString& ChannelName, const FRichCurve& Curve)
{
	TSharedPtr<FJsonObject> Chan = MakeShared<FJsonObject>();
	Chan->SetStringField(TEXT("name"), ChannelName);
	Chan->SetNumberField(TEXT("KeyCount"), Curve.Keys.Num());

	TArray<TSharedPtr<FJsonValue>> KeysArr;
	for (const FRichCurveKey& Key : Curve.Keys)
	{
		TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
		K->SetNumberField(TEXT("Time"),   Key.Time);
		K->SetNumberField(TEXT("Value"),  Key.Value);
		K->SetStringField(TEXT("Interp"), InterpToStr(Key.InterpMode));
		KeysArr.Add(MakeShared<FJsonValueObject>(K));
	}
	Chan->SetArrayField(TEXT("Keys"), KeysArr);
	return Chan;
}

void FNexusGetAssetCurveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetCurve");
	Out.SearchAssetTypes = {TEXT("CurveFloat"), TEXT("CurveVector"), TEXT("CurveLinearColor"), TEXT("CurveTable")};
	Out.Description = TEXT("Read curve asset channels and keyframes (CurveFloat/Vector/LinearColor/CurveTable).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("curve"), TEXT("keyframe"), TEXT("Channel"), TEXT("timeline"), TEXT("Read") };
	Out.RelatedCapabilities = { TEXT("CreateAssetCurve"), TEXT("ManageAssetCurve") };
}

FCapabilityResult FNexusGetAssetCurveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		// UCurveTable 不继承自 UCurveBase，需分开加载以避免 Cast<> 警告
		UObject* AssetObj = LoadObject<UObject>(nullptr, *AssetPath);
		if (!AssetObj)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load curve asset: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), AssetObj->GetName());
		Entry->SetStringField(TEXT("Path"), AssetObj->GetPathName());

		if (UCurveFloat* CF = Cast<UCurveFloat>(AssetObj))
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("CurveFloat"));
			TArray<TSharedPtr<FJsonValue>> ChansArr;
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("Value"), CF->FloatCurve)));
			Entry->SetArrayField(TEXT("Channels"), ChansArr);
		}
		else if (UCurveVector* CV = Cast<UCurveVector>(AssetObj))
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("CurveVector"));
			TArray<TSharedPtr<FJsonValue>> ChansArr;
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("X"), CV->FloatCurves[0])));
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("Y"), CV->FloatCurves[1])));
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("Z"), CV->FloatCurves[2])));
			Entry->SetArrayField(TEXT("Channels"), ChansArr);
		}
		else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(AssetObj))
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("CurveLinearColor"));
			TArray<TSharedPtr<FJsonValue>> ChansArr;
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("R"), CC->FloatCurves[0])));
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("G"), CC->FloatCurves[1])));
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("B"), CC->FloatCurves[2])));
			ChansArr.Add(MakeShared<FJsonValueObject>(SerializeChannel(TEXT("A"), CC->FloatCurves[3])));
			Entry->SetArrayField(TEXT("Channels"), ChansArr);
		}
		else if (UCurveTable* CT = Cast<UCurveTable>(AssetObj))
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("CurveTable"));
			TArray<TSharedPtr<FJsonValue>> RowsArr;
			// UE4.25+: GetRowMap() 返回 TMap<FName, FRealCurve*>；默认 RichCurves 模式下 static_cast 安全
			for (const auto& Pair : CT->GetRowMap())
			{
				FRichCurve* RC = static_cast<FRichCurve*>(Pair.Value);
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Pair.Key.ToString());
				Row->SetNumberField(TEXT("KeyCount"), RC ? RC->Keys.Num() : 0);
				RowsArr.Add(MakeShared<FJsonValueObject>(Row));
			}
			Entry->SetArrayField(TEXT("Rows"), RowsArr);
		}
		else
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("Unknown"));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetCurveCapability)
