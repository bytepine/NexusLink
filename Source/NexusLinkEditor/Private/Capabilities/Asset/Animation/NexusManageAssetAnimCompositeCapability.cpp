// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimSequenceBase.h"
#include "NexusMcpTool.h"

void FNexusManageAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetAnimComposite");
	Out.SearchAssetTypes = {TEXT("AnimComposite")};
	Out.Description = TEXT("Edit AnimComposite composite track segments. action: AddSegment/RemoveSegment.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"),        FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("AddSegment"), TEXT("RemoveSegment") }))
		.Prop(TEXT("AnimPath"),          FNexusSchema::Str(TEXT("AnimSequence path (AddSegment)")))
		.Prop(TEXT("StartPos"),          FNexusSchema::Num(TEXT("Segment start time (AddSegment; default end)")))
		.Prop(TEXT("AnimStartTime"),     FNexusSchema::Num(TEXT("Source anim start (AddSegment; default 0)")))
		.Prop(TEXT("AnimEndTime"),       FNexusSchema::Num(TEXT("Source anim end (AddSegment; 0=full length)")))
		.Prop(TEXT("PlayRate"),          FNexusSchema::Num(TEXT("Play rate (AddSegment; default 1.0)")))
		.Prop(TEXT("SegmentIndex"),      FNexusSchema::Int(TEXT("Segment index (RemoveSegment)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("AnimComposite asset path")))
		.Required(TEXT("Operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Composite"), TEXT("segment"), TEXT("Add"), TEXT("Remove"), TEXT("track") };
	Out.RelatedCapabilities = { TEXT("CreateAssetAnimComposite"), TEXT("GetAssetAnimComposite") };
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
	if (!Composite)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AnimComposite is null"));
		return;
	}
	const FString AnimPath = FNexusArgs(Op).Str(TEXT("AnimPath"));
	UAnimSequenceBase* AnimRef = AnimPath.IsEmpty()
		? nullptr
		: LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);

	float StartPos = 0.f;
	if (Op->HasField(TEXT("StartPos")))
	{
		StartPos = static_cast<float>(Op->GetNumberField(TEXT("StartPos")));
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
	NewSeg.AnimStartTime = static_cast<float>(FNexusArgs(Op).Num(TEXT("AnimStartTime"), 0.f));
	NewSeg.AnimEndTime   = static_cast<float>(FNexusArgs(Op).Num(TEXT("AnimEndTime"), 0.f));
	NewSeg.AnimPlayRate  = static_cast<float>(FNexusArgs(Op).Num(TEXT("PlayRate"), 1.f));
	NewSeg.LoopingCount  = 1;

#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
	if (AnimRef) NewSeg.SetAnimReference(AnimRef, false);
#else
	NewSeg.AnimReference = AnimRef;
#endif
	const int32 NewIdx = Composite->AnimationTrack.AnimSegments.Add(NewSeg);
	Ctx.Entry->SetNumberField(TEXT("NewIndex"), NewIdx);
	MarkCompDirty(Ctx);
}

static void HandleComp_RemoveSegment(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimComposite* Composite = CompFrom(Ctx);
	if (!Composite)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AnimComposite is null"));
		return;
	}
	if (!Op->HasField(TEXT("SegmentIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveSegment requires SegmentIndex"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("SegmentIndex")));
	if (!Composite->AnimationTrack.AnimSegments.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("SegmentIndex %d out of bounds"), Idx));
		return;
	}
	Composite->AnimationTrack.AnimSegments.RemoveAt(Idx);
	MarkCompDirty(Ctx);
}

bool FNexusManageAssetAnimCompositeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetAnimCompositeCapability::FinalizeTarget(void* Target) const
{
	FAnimCompositeActionState* State = static_cast<FAnimCompositeActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Composite)
	{
		State->Composite->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetAnimCompositeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddSegment"),    &HandleComp_AddSegment);
	OutHandlers.Add(TEXT("RemoveSegment"), &HandleComp_RemoveSegment);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetAnimCompositeCapability)
