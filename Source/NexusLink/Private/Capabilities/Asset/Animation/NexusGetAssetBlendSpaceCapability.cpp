// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetBlendSpaceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#if NX_UE_HAS_BLEND_SPACE_BASE
#include "Animation/BlendSpaceBase.h"
#endif
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "UObject/UnrealType.h"
#include "NexusMcpTool.h"

void FGetAssetBlendSpaceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_blend_space");
	Out.Description = TEXT("读取 BlendSpace 快照：轴参数 + 样本列表。写用 manage_asset_blend_space。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("BlendSpace 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 BlendSpace 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("axis"), TEXT("sample"), TEXT("locomotion"), TEXT("1d"), TEXT("2d") };
	Out.RelatedCapabilities = { TEXT("manage_asset_blend_space"), TEXT("create_asset_blend_space"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("读取 BlendSpace 轴定义与样本动画；写用 manage_asset_blend_space");
}

// 将 FBlendParameter 写入 JSON 对象
static void AppendAxisParam(const FBlendParameter& Param, TSharedPtr<FJsonObject>& AxisObj)
{
	AxisObj->SetStringField(TEXT("displayName"), Param.DisplayName);
	AxisObj->SetNumberField(TEXT("min"),          Param.Min);
	AxisObj->SetNumberField(TEXT("max"),          Param.Max);
	AxisObj->SetNumberField(TEXT("gridNum"),      Param.GridNum);
}

FCapabilityResult FGetAssetBlendSpaceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		if (!Arguments.IsValid()) { OutError = TEXT("需要 assetPath"); return; }

		FString Single;
		if (Arguments->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
			Paths.Add(Single);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Arguments->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{ FString P; if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty()) Paths.AddUnique(P); }

		if (Paths.Num() == 0) { OutError = TEXT("需要 assetPath 或 assetPaths"); return; }

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), Path);

			UBlendSpace* BS = FNexusAssetUtils::LoadAssetWithFallback<UBlendSpace>(Path);
			if (!BS)
			{
				Entry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("BlendSpace 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), BS->GetName());
			const bool b1D = BS->IsA<UBlendSpace1D>();
			Entry->SetStringField(TEXT("assetType"), b1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
			if (const USkeleton* Skel = BS->GetSkeleton())
				Entry->SetStringField(TEXT("skeleton"), Skel->GetPathName());

			// 轴参数（横轴 BlendParameters[0]，纵轴 BlendParameters[1]，仅2D有效）
			// BlendParameters 在所有版本均为 protected；UE5.0+ 提供 GetBlendParameter()，低版本走反射
			auto GetBlendParamSafe = [&](int32 Idx) -> const FBlendParameter*
			{
#if NX_UE_HAS_BLEND_SPACE_GET_BLEND_PARAMETER
				return &BS->GetBlendParameter(Idx);
#else
				UClass* BsClass = BS->GetClass();
				while (BsClass)
				{
					if (FArrayProperty* Prop = FindFProperty<FArrayProperty>(BsClass, TEXT("BlendParameters")))
					{
						// BlendParameters 是 C 数组，通过结构体属性访问
						break;
					}
					BsClass = BsClass->GetSuperClass();
				}
				// UE 4.26 无公开 accessor，通过反射取 C 数组地址（BlendParameters 是 UPROPERTY）
				// 此处用 FStructProperty 方式查找 array 实际地址
				if (FProperty* BpProp = BS->GetClass()->FindPropertyByName(TEXT("BlendParameters")))
				{
					// C 原生数组：property 是 FStructProperty，整体 3 个元素
					// 取第 Idx 个偏移
					if (FStructProperty* StructProp = CastField<FStructProperty>(BpProp))
					{
						uint8* RawBase = StructProp->ContainerPtrToValuePtr<uint8>(BS, 0);
						return reinterpret_cast<const FBlendParameter*>(RawBase) + Idx;
					}
				}
				return nullptr;
#endif
			};

			TArray<TSharedPtr<FJsonValue>> AxesArr;
			if (const FBlendParameter* Axis0 = GetBlendParamSafe(0))
			{
				TSharedPtr<FJsonObject> AxisObj0 = MakeShared<FJsonObject>();
				AppendAxisParam(*Axis0, AxisObj0);
				AxisObj0->SetNumberField(TEXT("axisIndex"), 0);
				AxesArr.Add(MakeShared<FJsonValueObject>(AxisObj0));
			}
			if (!b1D)
			{
				if (const FBlendParameter* Axis1 = GetBlendParamSafe(1))
				{
					TSharedPtr<FJsonObject> AxisObj1 = MakeShared<FJsonObject>();
					AppendAxisParam(*Axis1, AxisObj1);
					AxisObj1->SetNumberField(TEXT("axisIndex"), 1);
					AxesArr.Add(MakeShared<FJsonValueObject>(AxisObj1));
				}
			}
			Entry->SetArrayField(TEXT("axes"), AxesArr);

			// 样本列表
			const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
			TArray<TSharedPtr<FJsonValue>> SamplesArr;
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const FBlendSample& S = Samples[i];
				TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
				SObj->SetNumberField(TEXT("index"), i);
			SObj->SetNumberField(TEXT("x"),      S.SampleValue.X);
			SObj->SetNumberField(TEXT("y"),      S.SampleValue.Y);
#if NX_UE_HAS_BLEND_SAMPLE_IS_VALID
			SObj->SetBoolField(TEXT("isValid"),  S.bIsValid);
#endif
				if (S.Animation)
					SObj->SetStringField(TEXT("animation"), S.Animation->GetPathName());
				SamplesArr.Add(MakeShared<FJsonValueObject>(SObj));
			}
			Entry->SetArrayField(TEXT("samples"), SamplesArr);
			Entry->SetNumberField(TEXT("sampleCount"), Samples.Num());

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetBlendSpaceCapability)
