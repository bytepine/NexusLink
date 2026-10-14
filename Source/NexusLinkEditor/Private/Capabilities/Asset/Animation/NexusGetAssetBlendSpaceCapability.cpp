// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetBlendSpaceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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

void FNexusGetAssetBlendSpaceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetBlendSpace");
	Out.SearchAssetTypes = {TEXT("BlendSpace"), TEXT("BlendSpace1D")};
	Out.Description = TEXT("Read BlendSpace snapshot: axis params + samples. Use ManageAssetBlendSpace for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("BlendSpace asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("axis"), TEXT("sample"), TEXT("locomotion"), TEXT("1d"), TEXT("2d") };
	Out.RelatedCapabilities = { TEXT("ManageAssetBlendSpace"), TEXT("CreateAssetBlendSpace"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Read BlendSpace axis defs and sample anims; use ManageAssetBlendSpace for writes");
}

// 将 FBlendParameter 写入 JSON 对象
static void AppendAxisParam(const FBlendParameter& Param, TSharedPtr<FJsonObject>& AxisObj)
{
	AxisObj->SetStringField(TEXT("DisplayName"), Param.DisplayName);
	AxisObj->SetNumberField(TEXT("Min"),          Param.Min);
	AxisObj->SetNumberField(TEXT("Max"),          Param.Max);
	AxisObj->SetNumberField(TEXT("GridNum"),      Param.GridNum);
}

FCapabilityResult FNexusGetAssetBlendSpaceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		UBlendSpace* BS = FNexusAssetUtils::LoadAssetWithFallback<UBlendSpace>(Path);
		if (!BS)
		{
			Entry->SetStringField(TEXT("Error"),
				FString::Printf(TEXT("BlendSpace not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), BS->GetName());
		const bool bIsBlendSpace1D = BS->IsA(UBlendSpace1D::StaticClass());
		Entry->SetStringField(TEXT("AssetType"), bIsBlendSpace1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
		if (const USkeleton* Skel = BS->GetSkeleton())
			Entry->SetStringField(TEXT("Skeleton"), Skel->GetPathName());

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
			AxisObj0->SetNumberField(TEXT("AxisIndex"), 0);
			AxesArr.Add(MakeShared<FJsonValueObject>(AxisObj0));
		}
		if (!bIsBlendSpace1D)
		{
			if (const FBlendParameter* Axis1 = GetBlendParamSafe(1))
			{
				TSharedPtr<FJsonObject> AxisObj1 = MakeShared<FJsonObject>();
				AppendAxisParam(*Axis1, AxisObj1);
				AxisObj1->SetNumberField(TEXT("AxisIndex"), 1);
				AxesArr.Add(MakeShared<FJsonValueObject>(AxisObj1));
			}
		}
		Entry->SetArrayField(TEXT("Axes"), AxesArr);

		// 样本列表
		const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
		TArray<TSharedPtr<FJsonValue>> SamplesArr;
		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const FBlendSample& S = Samples[SampleIndex];
			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("Index"), SampleIndex);
			SObj->SetNumberField(TEXT("x"),      S.SampleValue.X);
			SObj->SetNumberField(TEXT("y"),      S.SampleValue.Y);
#if NX_UE_HAS_BLEND_SAMPLE_IS_VALID
			SObj->SetBoolField(TEXT("bIsValid"),  S.bIsValid);
#endif
			if (S.Animation)
				SObj->SetStringField(TEXT("Animation"), S.Animation->GetPathName());
			SamplesArr.Add(MakeShared<FJsonValueObject>(SObj));
		}
		Entry->SetArrayField(TEXT("Samples"), SamplesArr);
		Entry->SetNumberField(TEXT("SampleCount"), Samples.Num());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetBlendSpaceCapability)
