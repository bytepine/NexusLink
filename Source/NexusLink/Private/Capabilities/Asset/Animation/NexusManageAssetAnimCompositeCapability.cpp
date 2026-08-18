// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimSequenceBase.h"
#include "NexusMcpTool.h"

void FManageAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_anim_composite");
	Out.SearchAssetTypes = {TEXT("AnimComposite")};
	Out.Description = TEXT("Edit AnimComposite composite track segments. action: add_segment/remove_segment.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"),        FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("add_segment"), TEXT("remove_segment") }))
		.Prop(TEXT("animPath"),          FNexusSchema::Str(TEXT("AnimSequence path (add_segment)")))
		.Prop(TEXT("startPos"),          FNexusSchema::Num(TEXT("Segment start time (add_segment; default end)")))
		.Prop(TEXT("animStartTime"),     FNexusSchema::Num(TEXT("Source anim start (add_segment; default 0)")))
		.Prop(TEXT("animEndTime"),       FNexusSchema::Num(TEXT("Source anim end (add_segment; 0=full length)")))
		.Prop(TEXT("playRate"),          FNexusSchema::Num(TEXT("Play rate (add_segment; default 1.0)")))
		.Prop(TEXT("segmentIndex"),      FNexusSchema::Int(TEXT("Segment index (remove_segment)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("AnimComposite asset path")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("composite"), TEXT("segment"), TEXT("add"), TEXT("remove"), TEXT("track") };
	Out.RelatedCapabilities = { TEXT("create_asset_anim_composite"), TEXT("get_asset_anim_composite") };
}

struct FAnimCompositeActionState
{
	UAnimComposite* Composite = nullptr;
	bool bDirty = false;
};

static FAnimCompositeActionState* CompState(FNexusActionContext& Ctx)
{
	return static_cast<FAnimCompositeActionState*>(Ctx.Target);
}

static UAnimComposite* CompFrom(FNexusActionContext& Ctx)
{
	FAnimCompositeActionState* S = CompState(Ctx);
	return S ? S->Composite : nullptr;
}

static void MarkCompDirty(FNexusActionContext& Ctx)
{
	if (FAnimCompositeActionState* S = CompState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleComp_AddSegment(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimComposite* Composite = CompFrom(Ctx);
	const FString AnimPath = FNexusArgs(Op).Str(TEXT("animPath"));
	UAnimSequenceBase* AnimRef = AnimPath.IsEmpty()
		? nullptr
		: LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);

	float StartPos = 0.f;
	if (Op->HasField(TEXT("startPos")))
	{
		StartPos = static_cast<float>(Op->GetNumberField(TEXT("startPos")));
	}
	else
	{
		for (const FAnimSegment& Seg : Composite->AnimationTrack.AnimSegments)
		{
			const float AnimLen = (Seg.AnimEndTime > 0.f)
				? (Seg.AnimEndTime - Seg.AnimStartTime)
#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
				: (Seg.GetAnimReference() ? Seg.GetAnimReference()->GetPlayLength() - Seg.AnimStartTime : 0.f);
#else
				: (Seg.AnimReference ? Seg.AnimReference->GetPlayLength() - Seg.AnimStartTime : 0.f);
#endif
			StartPos = FMath::Max(StartPos, Seg.StartPos + AnimLen);
		}
	}

	FAnimSegment NewSeg;
	NewSeg.StartPos      = StartPos;
	NewSeg.AnimStartTime = static_cast<float>(FNexusArgs(Op).Num(TEXT("animStartTime"), 0.f));
	NewSeg.AnimEndTime   = static_cast<float>(FNexusArgs(Op).Num(TEXT("animEndTime"), 0.f));
	NewSeg.AnimPlayRate  = static_cast<float>(FNexusArgs(Op).Num(TEXT("playRate"), 1.f));
	NewSeg.LoopingCount  = 1;

#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
	if (AnimRef) NewSeg.SetAnimReference(AnimRef, false);
#else
	NewSeg.AnimReference = AnimRef;
#endif
	const int32 NewIdx = Composite->AnimationTrack.AnimSegments.Add(NewSeg);
	Ctx.Entry->SetNumberField(TEXT("newIndex"), NewIdx);
	MarkCompDirty(Ctx);
}

static void HandleComp_RemoveSegment(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimComposite* Composite = CompFrom(Ctx);
	if (!Op->HasField(TEXT("segmentIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_segment requires segmentIndex"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("segmentIndex")));
	if (!Composite->AnimationTrack.AnimSegments.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("segmentIndex %d out of bounds"), Idx));
		return;
	}
	Composite->AnimationTrack.AnimSegments.RemoveAt(Idx);
	MarkCompDirty(Ctx);
}

bool FManageAssetAnimCompositeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UAnimComposite* Composite = LoadObject<UAnimComposite>(nullptr, *AssetPath);
	if (!Composite)
	{
		OutError = FString::Printf(TEXT("Failed to load AnimComposite: %s"), *AssetPath);
		return false;
	}
	FAnimCompositeActionState* State = new FAnimCompositeActionState();
	State->Composite = Composite;
	OutTarget = State;
	return true;
}

void FManageAssetAnimCompositeCapability::FinalizeTarget(void* Target) const
{
	FAnimCompositeActionState* State = static_cast<FAnimCompositeActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Composite)
	{
		State->Composite->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetAnimCompositeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_segment"),    &HandleComp_AddSegment);
	OutHandlers.Add(TEXT("remove_segment"), &HandleComp_RemoveSegment);
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimCompositeCapability)
