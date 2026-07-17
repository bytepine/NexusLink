// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetBlendSpaceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#if NX_UE_HAS_BLEND_SPACE_BASE
#include "Animation/BlendSpaceBase.h"
#endif
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "UObject/UnrealType.h"
#include "NexusMcpTool.h"

// 跨版本获取 SampleData 的可写指针（SampleData 在 UE5.5+ 为 protected）
static TArray<FBlendSample>* GetSampleDataPtr(UBlendSpace* BS)
{
	if (!BS) return nullptr;
	FArrayProperty* Prop = FindFProperty<FArrayProperty>(BS->GetClass(), TEXT("SampleData"));
	if (!Prop) return nullptr;
	return Prop->ContainerPtrToValuePtr<TArray<FBlendSample>>(BS);
}

void FManageAssetBlendSpaceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_blend_space");
	Out.SearchAssetTypes = {TEXT("BlendSpace"), TEXT("BlendSpace1D")};
	Out.Description = TEXT("编辑 BlendSpace：set_axis / add_sample / remove_sample。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("set_axis"), TEXT("add_sample"), TEXT("remove_sample") }))
		.Prop(TEXT("axisIndex"),       FNexusSchema::Int(TEXT("轴索引：0=横轴, 1=纵轴（set_axis）")))
		.Prop(TEXT("displayName"),     FNexusSchema::Str(TEXT("轴显示名（set_axis）")))
		.Prop(TEXT("min"),             FNexusSchema::Num(TEXT("轴最小值（set_axis）")))
		.Prop(TEXT("max"),             FNexusSchema::Num(TEXT("轴最大值（set_axis）")))
		.Prop(TEXT("gridNum"),         FNexusSchema::Int(TEXT("轴格数（set_axis）")))
		.Prop(TEXT("animationPath"),   FNexusSchema::Str(TEXT("AnimSequence 路径（add_sample）")))
		.Prop(TEXT("x"),               FNexusSchema::Num(TEXT("横轴坐标（add/remove_sample）")))
		.Prop(TEXT("y"),               FNexusSchema::Num(TEXT("纵轴坐标（add/remove_sample，2D）")))
		.Prop(TEXT("sampleIndex"),     FNexusSchema::Int(TEXT("样本索引（remove_sample）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("BlendSpace 资产路径")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("axis"), TEXT("sample"), TEXT("locomotion") };
	Out.RelatedCapabilities = { TEXT("get_asset_blend_space"), TEXT("create_asset_blend_space") };
	Out.WhenToUse = TEXT("配置 BlendSpace 轴参数或添加/删除动画样本；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetBlendSpaceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UBlendSpace* BS = FNexusAssetUtils::LoadAssetWithFallback<UBlendSpace>(AssetPath);
		if (!BS)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("BlendSpace 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				TEXT("缺少 operations 数组"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("assetPath"), AssetPath);
			ResEntry->SetStringField(TEXT("action"),    Action);

			if (Action.Equals(TEXT("set_axis"), ESearchCase::IgnoreCase))
			{
				int32 AxisIdx = 0;
				if (Op->HasField(TEXT("axisIndex")))
					AxisIdx = static_cast<int32>(Op->GetNumberField(TEXT("axisIndex")));
				if (AxisIdx < 0 || AxisIdx > 2)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("axisIndex 范围 0-2"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				// BlendParameters 在所有版本均为 protected，通过反射取可写指针
				FBlendParameter* Param = nullptr;
				if (FProperty* BpProp = BS->GetClass()->FindPropertyByName(TEXT("BlendParameters")))
				{
					if (FStructProperty* StructProp = CastField<FStructProperty>(BpProp))
					{
						uint8* RawBase = StructProp->ContainerPtrToValuePtr<uint8>(BS, 0);
						Param = reinterpret_cast<FBlendParameter*>(RawBase) + AxisIdx;
					}
				}
				if (!Param)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("反射获取 BlendParameters 失败"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				FString Name;
				if (Op->TryGetStringField(TEXT("displayName"), Name)) Param->DisplayName = Name;
				double V = 0.0;
				if (Op->TryGetNumberField(TEXT("min"), V)) Param->Min = static_cast<float>(V);
				if (Op->TryGetNumberField(TEXT("max"), V)) Param->Max = static_cast<float>(V);
				double Grid = 0.0;
				if (Op->TryGetNumberField(TEXT("gridNum"), Grid)) Param->GridNum = static_cast<int32>(Grid);
				bDirty = true;
				ResEntry->SetNumberField(TEXT("axisIndex"), AxisIdx);
				ResEntry->SetBoolField(TEXT("success"), true);
			}
			else if (Action.Equals(TEXT("add_sample"), ESearchCase::IgnoreCase))
			{
				FString AnimPath;
				if (!Op->TryGetStringField(TEXT("animationPath"), AnimPath) || AnimPath.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_sample 需要 animationPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				UAnimSequence* Anim = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(AnimPath);
				if (!Anim)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("AnimSequence 未找到: %s"), *AnimPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
				if (!SampleData)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("无法获取 SampleData（反射失败）"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				double X = 0.0, Y = 0.0;
				Op->TryGetNumberField(TEXT("x"), X);
				Op->TryGetNumberField(TEXT("y"), Y);
				FBlendSample NewSample;
			NewSample.Animation  = Anim;
			NewSample.SampleValue = FVector(static_cast<float>(X), static_cast<float>(Y), 0.f);
#if NX_UE_HAS_BLEND_SAMPLE_IS_VALID
			NewSample.bIsValid    = true;
#endif
				const int32 NewIdx = SampleData->Add(NewSample);
				bDirty = true;
				ResEntry->SetNumberField(TEXT("sampleIndex"), NewIdx);
				ResEntry->SetStringField(TEXT("animation"),   AnimPath);
				ResEntry->SetBoolField(TEXT("success"),       true);
			}
			else if (Action.Equals(TEXT("remove_sample"), ESearchCase::IgnoreCase))
			{
				TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
				if (!SampleData)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("无法获取 SampleData（反射失败）"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				int32 SampleIdx = -1;
				if (Op->HasField(TEXT("sampleIndex")))
					SampleIdx = static_cast<int32>(Op->GetNumberField(TEXT("sampleIndex")));
				else
				{
					// 按坐标找
					double X = 0.0, Y = 0.0;
					Op->TryGetNumberField(TEXT("x"), X);
					Op->TryGetNumberField(TEXT("y"), Y);
					const FVector TargetVal(static_cast<float>(X), static_cast<float>(Y), 0.f);
					for (int32 i = 0; i < SampleData->Num(); ++i)
					{
						if (FVector::DistSquared((*SampleData)[i].SampleValue, TargetVal) < KINDA_SMALL_NUMBER)
						{
							SampleIdx = i;
							break;
						}
					}
				}
				if (!SampleData->IsValidIndex(SampleIdx))
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("样本索引无效或未找到匹配样本"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				SampleData->RemoveAt(SampleIdx);
				bDirty = true;
				ResEntry->SetNumberField(TEXT("removedIndex"), SampleIdx);
				ResEntry->SetBoolField(TEXT("removed"),        true);
			}
			else
			{
				ResEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			BS->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("已修改，用 save_asset 落盘"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetBlendSpaceCapability)
