// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimMontageCapability.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "NexusMcpTool.h"

/** 获取或创建指定名称的 SlotAnimationTrack，返回其下标 */
static int32 FindOrCreateSlot(UAnimMontage* Montage, const FName& SlotName)
{
	for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
	{
		if (Montage->SlotAnimTracks[i].SlotName == SlotName) { return i; }
	}
	FSlotAnimationTrack NewTrack;
	NewTrack.SlotName = SlotName;
	return Montage->SlotAnimTracks.Add(NewTrack);
}

/** 计算指定 SlotTrack 的末尾时间（最后一个 Segment 的 StartPos + 实际时长） */
static float CalcSlotEndTime(const FSlotAnimationTrack& Track)
{
	float End = 0.0f;
	for (const FAnimSegment& Seg : Track.AnimTrack.AnimSegments)
	{
		const float AnimLen = (Seg.AnimEndTime > 0.0f)
			? (Seg.AnimEndTime - Seg.AnimStartTime)
#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
			: (Seg.GetAnimReference() ? Seg.GetAnimReference()->GetPlayLength() - Seg.AnimStartTime : 0.0f);
#else
			: (Seg.AnimReference ? Seg.AnimReference->GetPlayLength() - Seg.AnimStartTime : 0.0f);
#endif
		const float SegEnd = Seg.StartPos + AnimLen;
		End = FMath::Max(End, SegEnd);
	}
	return End;
}

void FManageAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_anim_montage");
	Out.SearchAssetTypes = {TEXT("AnimMontage")};
	Out.Description = TEXT("Batch edit Montage structure. persist with save_asset.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("add_segment"), TEXT("remove_segment"), TEXT("add_section"), TEXT("remove_section") }))
		// add_segment
		.Prop(TEXT("animSequencePath"), FNexusSchema::Str(TEXT("AnimSequence path (add_segment)")))
		.Prop(TEXT("slotName"),         FNexusSchema::Str(TEXT("Slot name"), TEXT("DefaultSlot")))
		.Prop(TEXT("startPos"),         FNexusSchema::Num(TEXT("Montage start position (sec); default append to end")))
		.Prop(TEXT("animStartTime"),    FNexusSchema::Num(TEXT("Anim start time (sec); default 0")))
		.Prop(TEXT("animEndTime"),      FNexusSchema::Num(TEXT("Anim end time (sec); default full length")))
		// remove_segment
		.Prop(TEXT("segmentIndex"),     FNexusSchema::Int(TEXT("Segment index to remove (remove_segment)"), TNumericLimits<int64>::Min(), 0))
		// add_section / remove_section
		.Prop(TEXT("sectionName"),      FNexusSchema::Str(TEXT("Section name (add_section/remove_section)")))
		.Prop(TEXT("sectionStartTime"), FNexusSchema::Num(TEXT("Section start time in Montage (sec) (add_section)")))
		.Prop(TEXT("nextSectionName"),  FNexusSchema::Str(TEXT("Loop to next section (add_section, optional)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("AnimMontage asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("montage"), TEXT("segment"), TEXT("section"), TEXT("slot"), TEXT("timeline")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_anim_montage"), TEXT("create_asset_anim_montage"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove Montage segments/sections/slots");
}

static UAnimMontage* MontageFrom(FNexusActionContext& Ctx)
{
	return static_cast<UAnimMontage*>(Ctx.Target);
}

static FString SlotNameFromOp(const TSharedPtr<FJsonObject>& Op)
{
	FString SlotNameStr = TEXT("DefaultSlot");
	Op->TryGetStringField(TEXT("slotName"), SlotNameStr);
	return SlotNameStr;
}

static void SyncMontageLength(UAnimMontage* Montage)
{
	const float NewLen = Montage->CalculateSequenceLength();
#if NX_UE_HAS_ANIM_COMPOSITE_SET_LENGTH
	Montage->SetCompositeLength(NewLen);
#elif WITH_EDITOR
	Montage->SetSequenceLength(NewLen);
#else
	Montage->SequenceLength = NewLen;
#endif
}

static void HandleMontage_AddSegment(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimMontage* Montage = MontageFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SeqPath = A.Str(TEXT("animSequencePath"));
	if (SeqPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_segment requires animSequencePath"));
		return;
	}

	UAnimSequenceBase* AnimSeq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequenceBase>(SeqPath);
	if (!AnimSeq)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimSequence not found: %s"), *SeqPath));
		return;
	}

	const FString SlotNameStr = SlotNameFromOp(Op);
	const FName SlotName(*SlotNameStr);

	const float FullLen       = AnimSeq->GetPlayLength();
	const float AnimStartTime = static_cast<float>(A.Num(TEXT("animStartTime"), 0.0f));
	const float AnimEndTime   = static_cast<float>(A.Num(TEXT("animEndTime"), FullLen));

	const int32 SlotIdx = FindOrCreateSlot(Montage, SlotName);
	FSlotAnimationTrack& Track = Montage->SlotAnimTracks[SlotIdx];

	// 未指定 startPos 时自动追加到 slot 末尾
	const float StartPos = static_cast<float>(A.Num(TEXT("startPos"), CalcSlotEndTime(Track)));

	FAnimSegment Segment;
#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
	Segment.SetAnimReference(AnimSeq);
#else
	Segment.AnimReference  = AnimSeq;
#endif
	Segment.AnimStartTime  = AnimStartTime;
	Segment.AnimEndTime    = AnimEndTime;
	Segment.StartPos       = StartPos;
	Segment.LoopingCount   = 1;

	Track.AnimTrack.AnimSegments.Add(Segment);
	// 同步 Montage 总时长，否则 Montage_Play 对空长度返回 0
	SyncMontageLength(Montage);
	Montage->MarkPackageDirty();

	Ctx.Entry->SetStringField(TEXT("slotName"),         SlotNameStr);
	Ctx.Entry->SetStringField(TEXT("animSequencePath"), AnimSeq->GetPathName());
	Ctx.Entry->SetNumberField(TEXT("startPos"),         StartPos);
	Ctx.Entry->SetNumberField(TEXT("animStartTime"),    AnimStartTime);
	Ctx.Entry->SetNumberField(TEXT("animEndTime"),      AnimEndTime);
	Ctx.Entry->SetNumberField(TEXT("segmentIndex"),     Track.AnimTrack.AnimSegments.Num() - 1);
	Ctx.Entry->SetNumberField(TEXT("sequenceLength"),   Montage->GetPlayLength());
}

static void HandleMontage_RemoveSegment(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimMontage* Montage = MontageFrom(Ctx);
	const FString SlotNameStr = SlotNameFromOp(Op);
	const FName SlotName(*SlotNameStr);

	int32 SlotIdx = INDEX_NONE;
	for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
	{
		if (Montage->SlotAnimTracks[i].SlotName == SlotName) { SlotIdx = i; break; }
	}
	if (SlotIdx == INDEX_NONE)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Slot '%s' not found"), *SlotNameStr));
		return;
	}

	if (!Op->HasField(TEXT("segmentIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_segment requires segmentIndex"));
		return;
	}
	const int32 SegIdx = static_cast<int32>(Op->GetNumberField(TEXT("segmentIndex")));

	TArray<FAnimSegment>& Segs = Montage->SlotAnimTracks[SlotIdx].AnimTrack.AnimSegments;
	if (SegIdx < 0 || SegIdx >= Segs.Num())
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("segmentIndex %d out of range [0, %d)"), SegIdx, Segs.Num()));
		return;
	}

	Segs.RemoveAt(SegIdx);
	SyncMontageLength(Montage);
	Montage->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("slotName"), SlotNameStr);
	Ctx.Entry->SetNumberField(TEXT("segmentIndex"), SegIdx);
	Ctx.Entry->SetNumberField(TEXT("sequenceLength"), Montage->GetPlayLength());
}

static void HandleMontage_AddSection(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimMontage* Montage = MontageFrom(Ctx);
	const FString SectionName = FNexusArgs(Op).Str(TEXT("sectionName"));
	if (SectionName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_section requires sectionName"));
		return;
	}

	for (const FCompositeSection& Sec : Montage->CompositeSections)
	{
		if (Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase))
		{
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Section '%s' already exists"), *SectionName));
			return;
		}
	}

	float SectionStartTime = 0.0f;
	if (Op->HasField(TEXT("sectionStartTime")))
	{
		SectionStartTime = static_cast<float>(Op->GetNumberField(TEXT("sectionStartTime")));
	}

	FCompositeSection NewSection;
	NewSection.SectionName = FName(*SectionName);
	// UE4 FCompositeSection 继承自 FAnimLinkableElement，通过 SetTime 写入时间
	NewSection.SetTime(SectionStartTime);

	const FString NextSectionName = FNexusArgs(Op).Str(TEXT("nextSectionName"));
	if (!NextSectionName.IsEmpty())
	{
		NewSection.NextSectionName = FName(*NextSectionName);
	}

	Montage->CompositeSections.Add(NewSection);
	Montage->CompositeSections.Sort([](const FCompositeSection& A, const FCompositeSection& B)
	{
		return A.GetTime() < B.GetTime();
	});
	Montage->MarkPackageDirty();

	Ctx.Entry->SetStringField(TEXT("sectionName"),      SectionName);
	Ctx.Entry->SetNumberField(TEXT("sectionStartTime"), SectionStartTime);
	if (!NextSectionName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("nextSectionName"), NextSectionName);
	}
}

static void HandleMontage_RemoveSection(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimMontage* Montage = MontageFrom(Ctx);
	const FString SectionName = FNexusArgs(Op).Str(TEXT("sectionName"));
	if (SectionName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_section requires sectionName"));
		return;
	}

	const int32 Removed = Montage->CompositeSections.RemoveAll([&SectionName](const FCompositeSection& Sec)
	{
		return Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase);
	});

	if (Removed == 0)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Section '%s' not found"), *SectionName));
	}
	else
	{
		Montage->MarkPackageDirty();
		Ctx.Entry->SetStringField(TEXT("sectionName"), SectionName);
	}
}

bool FManageAssetAnimMontageCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UAnimMontage* Montage = FNexusAssetUtils::LoadAssetWithFallback<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		OutError = FString::Printf(TEXT("AnimMontage not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Montage;
	return true;
}

void FManageAssetAnimMontageCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_segment"),    &HandleMontage_AddSegment);
	OutHandlers.Add(TEXT("remove_segment"), &HandleMontage_RemoveSegment);
	OutHandlers.Add(TEXT("add_section"),    &HandleMontage_AddSection);
	OutHandlers.Add(TEXT("remove_section"), &HandleMontage_RemoveSection);
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimMontageCapability)
