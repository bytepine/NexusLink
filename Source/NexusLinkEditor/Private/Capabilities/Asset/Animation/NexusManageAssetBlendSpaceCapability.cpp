// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetBlendSpaceCapability.h"
#include "Utils/NexusArgs.h"
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

struct FBlendSpaceActionState
{
	UBlendSpace* BS = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FBlendSpaceActionState* BSState(FNexusActionContext& Ctx)
{
	return static_cast<FBlendSpaceActionState*>(Ctx.Target);
}

static UBlendSpace* BSFrom(FNexusActionContext& Ctx)
{
	FBlendSpaceActionState* S = BSState(Ctx);
	return S ? S->BS : nullptr;
}

static void MarkBSDirty(FNexusActionContext& Ctx)
{
	if (FBlendSpaceActionState* S = BSState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleBS_SetAxis(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlendSpace* BS = BSFrom(Ctx);
	int32 AxisIdx = 0;
	if (Op->HasField(TEXT("axisIndex")))
		AxisIdx = static_cast<int32>(Op->GetNumberField(TEXT("axisIndex")));
	if (AxisIdx < 0 || AxisIdx > 2)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("axisIndex range 0-2"));
		return;
	}
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Reflection failed to get BlendParameters"));
		return;
	}
	FString Name;
	if (Op->TryGetStringField(TEXT("displayName"), Name)) Param->DisplayName = Name;
	double V = 0.0;
	if (Op->TryGetNumberField(TEXT("min"), V)) Param->Min = static_cast<float>(V);
	if (Op->TryGetNumberField(TEXT("max"), V)) Param->Max = static_cast<float>(V);
	double Grid = 0.0;
	if (Op->TryGetNumberField(TEXT("gridNum"), Grid)) Param->GridNum = static_cast<int32>(Grid);
	MarkBSDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("axisIndex"), AxisIdx);
}

static void HandleBS_AddSample(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlendSpace* BS = BSFrom(Ctx);
	FString AnimPath;
	if (!Op->TryGetStringField(TEXT("animationPath"), AnimPath) || AnimPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_sample requires animationPath"));
		return;
	}
	UAnimSequence* Anim = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(AnimPath);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("AnimSequence not found: %s"), *AnimPath));
		return;
	}
	TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
	if (!SampleData)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to get SampleData (reflection failed)"));
		return;
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
	MarkBSDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("sampleIndex"), NewIdx);
	Ctx.Entry->SetStringField(TEXT("animation"),   AnimPath);
}

static void HandleBS_RemoveSample(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlendSpace* BS = BSFrom(Ctx);
	TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
	if (!SampleData)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to get SampleData (reflection failed)"));
		return;
	}
	int32 SampleIdx = -1;
	if (Op->HasField(TEXT("sampleIndex")))
		SampleIdx = static_cast<int32>(Op->GetNumberField(TEXT("sampleIndex")));
	else
	{
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Invalid sample index or no matching sample"));
		return;
	}
	SampleData->RemoveAt(SampleIdx);
	MarkBSDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("removedIndex"), SampleIdx);
	Ctx.Entry->SetBoolField(TEXT("removed"),        true);
}

bool FManageAssetBlendSpaceCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UBlendSpace* BS = FNexusAssetUtils::LoadAssetWithFallback<UBlendSpace>(AssetPath);
	if (!BS)
	{
		OutError = FString::Printf(TEXT("BlendSpace not found: %s"), *AssetPath);
		return false;
	}
	FBlendSpaceActionState* State = new FBlendSpaceActionState();
	State->BS = BS;
	OutTarget = State;
	return true;
}

void FManageAssetBlendSpaceCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Args;
	if (FBlendSpaceActionState* State = static_cast<FBlendSpaceActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
}

void FManageAssetBlendSpaceCapability::FinalizeTarget(void* Target) const
{
	FBlendSpaceActionState* State = static_cast<FBlendSpaceActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->BS)
	{
		State->BS->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	}
	delete State;
}

void FManageAssetBlendSpaceCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_axis"),      &HandleBS_SetAxis);
	OutHandlers.Add(TEXT("add_sample"),    &HandleBS_AddSample);
	OutHandlers.Add(TEXT("remove_sample"), &HandleBS_RemoveSample);
}

REGISTER_MCP_CAPABILITY(FManageAssetBlendSpaceCapability)
