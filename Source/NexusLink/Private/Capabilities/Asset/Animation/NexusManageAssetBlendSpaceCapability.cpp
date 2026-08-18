// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetBlendSpaceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("Edit BlendSpace: set_axis / add_sample / remove_sample.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_axis"), TEXT("add_sample"), TEXT("remove_sample") }))
		.Prop(TEXT("axisIndex"),       FNexusSchema::Int(TEXT("Axis index: 0=horizontal, 1=vertical (set_axis)")))
		.Prop(TEXT("displayName"),     FNexusSchema::Str(TEXT("Axis display name (set_axis)")))
		.Prop(TEXT("min"),             FNexusSchema::Num(TEXT("Axis minimum (set_axis)")))
		.Prop(TEXT("max"),             FNexusSchema::Num(TEXT("Axis maximum (set_axis)")))
		.Prop(TEXT("gridNum"),         FNexusSchema::Int(TEXT("Axis grid divisions (set_axis)")))
		.Prop(TEXT("animationPath"),   FNexusSchema::Str(TEXT("AnimSequence path (add_sample)")))
		.Prop(TEXT("x"),               FNexusSchema::Num(TEXT("Horizontal axis coordinate (add/remove_sample)")))
		.Prop(TEXT("y"),               FNexusSchema::Num(TEXT("Vertical axis coordinate (add/remove_sample, 2D)")))
		.Prop(TEXT("sampleIndex"),     FNexusSchema::Int(TEXT("Sample index (remove_sample)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("BlendSpace asset path")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("axis"), TEXT("sample"), TEXT("locomotion") };
	Out.RelatedCapabilities = { TEXT("get_asset_blend_space"), TEXT("create_asset_blend_space") };
	Out.WhenToUse = TEXT("Configure BlendSpace axes or samples; persist with save_asset after changes");
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
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("BlendSpace not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		if (OpsArr.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("Missing operations array"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("path"), AssetPath);
			ResEntry->SetStringField(TEXT("action"),    Action);

			if (Action.Equals(TEXT("set_axis"), ESearchCase::IgnoreCase))
			{
				int32 AxisIdx = 0;
				if (Op->HasField(TEXT("axisIndex")))
					AxisIdx = static_cast<int32>(Op->GetNumberField(TEXT("axisIndex")));
				if (AxisIdx < 0 || AxisIdx > 2)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("axisIndex range 0-2"));
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
					ResEntry->SetStringField(TEXT("error"), TEXT("Reflection failed to get BlendParameters"));
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
			}
			else if (Action.Equals(TEXT("add_sample"), ESearchCase::IgnoreCase))
			{
				FString AnimPath;
				if (!Op->TryGetStringField(TEXT("animationPath"), AnimPath) || AnimPath.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_sample requires animationPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				UAnimSequence* Anim = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(AnimPath);
				if (!Anim)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("AnimSequence not found: %s"), *AnimPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
					continue;
				}
				TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
				if (!SampleData)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get SampleData (reflection failed)"));
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
			}
			else if (Action.Equals(TEXT("remove_sample"), ESearchCase::IgnoreCase))
			{
				TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
				if (!SampleData)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get SampleData (reflection failed)"));
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
					ResEntry->SetStringField(TEXT("error"), TEXT("Invalid sample index or no matching sample"));
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
					FString::Printf(TEXT("Unknown action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			BS->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetBlendSpaceCapability)
