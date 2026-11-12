// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetBlendSpaceCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusBlendSpaceCompat.h"
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
static TArray<FBlendSample>* GetSampleDataPtr(FNexusBlendSpaceAsset* BS)
{
	if (!BS) return nullptr;
	FArrayProperty* Prop = FindFProperty<FArrayProperty>(BS->GetClass(), TEXT("SampleData"));
	if (!Prop) return nullptr;
	return Prop->ContainerPtrToValuePtr<TArray<FBlendSample>>(BS);
}

void FNexusManageAssetBlendSpaceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetBlendSpace");
	Out.SearchAssetTypes = {TEXT("BlendSpace"), TEXT("BlendSpace1D")};
	Out.Description = TEXT("Edit BlendSpace: SetAxis / AddSample / RemoveSample.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetAxis"), TEXT("AddSample"), TEXT("RemoveSample") }))
		.Prop(TEXT("AxisIndex"),       FNexusSchema::Int(TEXT("Axis index: 0 for 1D; 0=horizontal, 1=vertical for 2D (SetAxis)")))
		.Prop(TEXT("DisplayName"),     FNexusSchema::Str(TEXT("Axis display name (SetAxis)")))
		.Prop(TEXT("Min"),             FNexusSchema::Num(TEXT("Axis minimum (SetAxis)")))
		.Prop(TEXT("Max"),             FNexusSchema::Num(TEXT("Axis maximum (SetAxis)")))
		.Prop(TEXT("GridNum"),         FNexusSchema::Int(TEXT("Axis grid divisions, 1-256 (SetAxis)")))
		.Prop(TEXT("AnimationPath"),   FNexusSchema::Str(TEXT("AnimSequence path (AddSample)")))
		.Prop(TEXT("X"),               FNexusSchema::Num(TEXT("Horizontal axis coordinate (add/RemoveSample)")))
		.Prop(TEXT("Y"),               FNexusSchema::Num(TEXT("Vertical axis coordinate (add/RemoveSample, 2D)")))
		.Prop(TEXT("SampleIndex"),     FNexusSchema::Int(TEXT("Sample index (RemoveSample)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("BlendSpace asset path")))
		.Required(TEXT("Operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("axis"), TEXT("sample"), TEXT("locomotion") };
	Out.RelatedCapabilities = { TEXT("GetAssetBlendSpace"), TEXT("CreateAssetBlendSpace") };
	Out.WhenToUse = TEXT("Configure BlendSpace axes or samples; persist with SaveAsset after changes");
}

struct FBlendSpaceActionState
{
	FNexusBlendSpaceAsset* BS = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FBlendSpaceActionState* BSState(FNexusActionContext& Ctx)
{
	return static_cast<FBlendSpaceActionState*>(Ctx.Target);
}

static FNexusBlendSpaceAsset* BSFrom(FNexusActionContext& Ctx)
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
	FNexusBlendSpaceAsset* BS = BSFrom(Ctx);
	if (!BS)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BlendSpace is null"));
		return;
	}
	int32 AxisIdx = 0;
	if (Op->HasField(TEXT("AxisIndex")))
		AxisIdx = static_cast<int32>(Op->GetNumberField(TEXT("AxisIndex")));
	const int32 AxisCount = BS->IsA(UBlendSpace1D::StaticClass()) ? 1 : 2;
	if (AxisIdx < 0 || AxisIdx >= AxisCount)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AxisIndex must be 0 for 1D, or 0-1 for 2D"));
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
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Reflection failed to get BlendParameters"));
		return;
	}
	FBlendParameter Updated = *Param;
	FString Name;
	if (Op->TryGetStringField(TEXT("DisplayName"), Name)) Updated.DisplayName = Name;
	double V = 0.0;
	if (Op->TryGetNumberField(TEXT("Min"), V)) Updated.Min = static_cast<float>(V);
	if (Op->TryGetNumberField(TEXT("Max"), V)) Updated.Max = static_cast<float>(V);
	double Grid = 0.0;
	if (Op->TryGetNumberField(TEXT("GridNum"), Grid))
	{
		if (!FMath::IsFinite(Grid) || Grid < 1 || Grid > 256 || Grid != static_cast<int32>(Grid))
		{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("GridNum must be an integer in 1-256")); return; }
		Updated.GridNum = static_cast<int32>(Grid);
	}
	if (!FMath::IsFinite(Updated.Min) || !FMath::IsFinite(Updated.Max) || Updated.Max <= Updated.Min)
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Axis bounds must be finite and Max > Min")); return; }
	BS->Modify();
	*Param = Updated;
	MarkBSDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("AxisIndex"), AxisIdx);
}

static void HandleBS_AddSample(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusBlendSpaceAsset* BS = BSFrom(Ctx);
	FString AnimPath;
	if (!Op->TryGetStringField(TEXT("AnimationPath"), AnimPath) || AnimPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddSample requires AnimationPath"));
		return;
	}
	UAnimSequence* Anim = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(AnimPath);
	if (!Anim)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("AnimSequence not found: %s"), *AnimPath));
		return;
	}
	TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
	if (!SampleData)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to get SampleData (reflection failed)"));
		return;
	}
	double X = 0.0, Y = 0.0;
	Op->TryGetNumberField(TEXT("X"), X);
	Op->TryGetNumberField(TEXT("Y"), Y);
	const FVector SampleValue(static_cast<float>(X), static_cast<float>(Y), 0.f);
	if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || (BS->IsA(UBlendSpace1D::StaticClass()) && Y != 0)
		|| !BS->ValidateAnimationSequence(Anim) || !BS->ValidateSampleValue(SampleValue))
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Invalid sample position, duplicate sample or incompatible animation")); return; }
	BS->Modify();
	const int32 NewIdx = SampleData->Num();
	BS->AddSample(Anim, SampleValue);
	if (SampleData->Num() != NewIdx + 1)
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Engine rejected blend sample")); return; }
	MarkBSDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("SampleIndex"), NewIdx);
	Ctx.Entry->SetStringField(TEXT("Animation"),   AnimPath);
}

static void HandleBS_RemoveSample(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusBlendSpaceAsset* BS = BSFrom(Ctx);
	TArray<FBlendSample>* SampleData = GetSampleDataPtr(BS);
	if (!SampleData)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to get SampleData (reflection failed)"));
		return;
	}
	int32 SampleIdx = -1;
	if (Op->HasField(TEXT("SampleIndex")))
		SampleIdx = static_cast<int32>(Op->GetNumberField(TEXT("SampleIndex")));
	else
	{
		double X = 0.0, Y = 0.0;
		Op->TryGetNumberField(TEXT("X"), X);
		Op->TryGetNumberField(TEXT("Y"), Y);
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
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Invalid sample index or no matching sample"));
		return;
	}
	BS->Modify();
	BS->DeleteSample(SampleIdx);
	MarkBSDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("RemovedIndex"), SampleIdx);
	Ctx.Entry->SetBoolField(TEXT("bRemoved"),        true);
}

bool FNexusManageAssetBlendSpaceCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	FNexusBlendSpaceAsset* BS = FNexusAssetUtils::LoadAssetWithFallback<FNexusBlendSpaceAsset>(AssetPath);
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

void FNexusManageAssetBlendSpaceCapability::AfterPrepareTarget(
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

static void RebuildBlendSpace(FNexusBlendSpaceAsset* BS)
{
	BS->ValidateSampleData();
#if NX_UE_HAS_BLEND_SPACE_BASE
	if (!BS->IsA(UBlendSpace1D::StaticClass())) return;
	// UE4 的重采样只在 Persona 私有 UI 中；1D 直接用相邻样本生成原生插值网格。
	const auto& Samples = BS->GetBlendSamples();
	TArray<int32> Indices;
	for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		if (Samples[SampleIndex].bIsValid) Indices.Add(SampleIndex);
	Indices.Sort([&](int32 Left, int32 Right) { return Samples[Left].SampleValue.X < Samples[Right].SampleValue.X; });
	BS->EmptyGridElements();
	if (Indices.Num() == 0) return;
	const FBlendParameter& Axis = BS->GetBlendParameter(0);
	if (Axis.GridNum < 1 || Axis.GridNum > 256 || !FMath::IsFinite(Axis.Min) || !FMath::IsFinite(Axis.Max) || Axis.Max <= Axis.Min) return;
	TArray<FEditorElement> Grid;
	for (int32 GridIndex = 0; GridIndex <= Axis.GridNum; ++GridIndex)
	{
		const float Position = FMath::Lerp(Axis.Min, Axis.Max, static_cast<float>(GridIndex) / Axis.GridNum);
		int32 Right = 0;
		while (Right + 1 < Indices.Num() && Samples[Indices[Right]].SampleValue.X < Position) ++Right;
		const int32 Left = FMath::Max(0, Right - 1);
		FEditorElement Element;
		Element.Indices[0] = Left;
		Element.Weights[0] = 1.f;
		const float Distance = Samples[Indices[Right]].SampleValue.X - Samples[Indices[Left]].SampleValue.X;
		if (Distance > SMALL_NUMBER)
		{
			const float Weight = FMath::Clamp((Position - Samples[Indices[Left]].SampleValue.X) / Distance, 0.f, 1.f);
			Element.Weights[0] = 1.f - Weight;
			Element.Indices[1] = Right;
			Element.Weights[1] = Weight;
		}
		Grid.Add(Element);
	}
	BS->FillupGridElements(Indices, Grid);
#else
	BS->ResampleData();
#endif
}

void FNexusManageAssetBlendSpaceCapability::FinalizeTarget(void* Target) const
{
	FBlendSpaceActionState* State = static_cast<FBlendSpaceActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->BS)
	{
		RebuildBlendSpace(State->BS);
		State->BS->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Note"), TEXT("Modified; persist with SaveAsset"));
#if NX_UE_HAS_BLEND_SPACE_BASE
			if (!State->BS->IsA(UBlendSpace1D::StaticClass()))
				State->OutTop->SetStringField(TEXT("RebuildNote"), TEXT("UE4 2D interpolation grid requires rebuilding in the BlendSpace editor before use"));
#endif
		}
	}
	delete State;
}

void FNexusManageAssetBlendSpaceCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetAxis"),      &HandleBS_SetAxis);
	OutHandlers.Add(TEXT("AddSample"),    &HandleBS_AddSample);
	OutHandlers.Add(TEXT("RemoveSample"), &HandleBS_RemoveSample);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetBlendSpaceCapability)
