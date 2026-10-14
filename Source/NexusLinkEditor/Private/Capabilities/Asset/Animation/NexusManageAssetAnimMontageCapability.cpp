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

void FNexusManageAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetAnimMontage");
	Out.SearchAssetTypes = {TEXT("AnimMontage")};
	Out.Description = TEXT("Batch edit Montage structure. persist with SaveAsset.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),           FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("AddSegment"), TEXT("RemoveSegment"), TEXT("AddSection"), TEXT("RemoveSection") }))
		// AddSegment
		.Prop(TEXT("AnimSequencePath"), FNexusSchema::Str(TEXT("AnimSequence path (AddSegment)")))
		.Prop(TEXT("SlotName"),         FNexusSchema::Str(TEXT("Slot name"), TEXT("DefaultSlot")))
		.Prop(TEXT("StartPos"),         FNexusSchema::Num(TEXT("Montage start position (sec); default append to end")))
		.Prop(TEXT("AnimStartTime"),    FNexusSchema::Num(TEXT("Anim start time (sec); default 0")))
		.Prop(TEXT("AnimEndTime"),      FNexusSchema::Num(TEXT("Anim end time (sec); default full length")))
		// RemoveSegment
		.Prop(TEXT("SegmentIndex"),     FNexusSchema::Int(TEXT("Segment index to remove (RemoveSegment)"), TNumericLimits<int64>::Min(), 0))
		// AddSection / RemoveSection
		.Prop(TEXT("SectionName"),      FNexusSchema::Str(TEXT("Section name (AddSection/RemoveSection)")))
		.Prop(TEXT("SectionStartTime"), FNexusSchema::Num(TEXT("Section start time in Montage (sec) (AddSection)")))
		.Prop(TEXT("NextSectionName"),  FNexusSchema::Str(TEXT("Loop to next section (AddSection, optional)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("AnimMontage asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("Montage"), TEXT("segment"), TEXT("Section"), TEXT("slot"), TEXT("timeline")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetAnimMontage"), TEXT("CreateAssetAnimMontage"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Write ops: add/remove Montage segments/sections/slots");
}

static UAnimMontage* MontageFrom(FNexusActionContext& Ctx)
{
	return static_cast<UAnimMontage*>(Ctx.Target);
}

static FString SlotNameFromOp(const TSharedPtr<FJsonObject>& Op)
{
	FString SlotNameStr = TEXT("DefaultSlot");
	Op->TryGetStringField(TEXT("SlotName"), SlotNameStr);
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
	const FString SeqPath = A.Str(TEXT("AnimSequencePath"));
	if (SeqPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddSegment requires AnimSequencePath"));
		return;
	}

	UAnimSequenceBase* AnimSeq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequenceBase>(SeqPath);
	if (!AnimSeq)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("AnimSequence not found: %s"), *SeqPath));
		return;
	}

	const FString SlotNameStr = SlotNameFromOp(Op);
	const FName SlotName(*SlotNameStr);

	const float FullLen       = AnimSeq->GetPlayLength();
	const float AnimStartTime = static_cast<float>(A.Num(TEXT("AnimStartTime"), 0.0f));
	const float AnimEndTime   = static_cast<float>(A.Num(TEXT("AnimEndTime"), FullLen));

	const int32 SlotIdx = FindOrCreateSlot(Montage, SlotName);
	FSlotAnimationTrack& Track = Montage->SlotAnimTracks[SlotIdx];

	// 未指定 StartPos 时自动追加到 slot 末尾
	const float StartPos = static_cast<float>(A.Num(TEXT("StartPos"), CalcSlotEndTime(Track)));

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

	Ctx.Entry->SetStringField(TEXT("SlotName"),         SlotNameStr);
	Ctx.Entry->SetStringField(TEXT("AnimSequencePath"), AnimSeq->GetPathName());
	Ctx.Entry->SetNumberField(TEXT("StartPos"),         StartPos);
	Ctx.Entry->SetNumberField(TEXT("AnimStartTime"),    AnimStartTime);
	Ctx.Entry->SetNumberField(TEXT("AnimEndTime"),      AnimEndTime);
	Ctx.Entry->SetNumberField(TEXT("SegmentIndex"),     Track.AnimTrack.AnimSegments.Num() - 1);
	Ctx.Entry->SetNumberField(TEXT("SequenceLength"),   Montage->GetPlayLength());
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
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Slot '%s' not found"), *SlotNameStr));
		return;
	}

	if (!Op->HasField(TEXT("SegmentIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveSegment requires SegmentIndex"));
		return;
	}
	const int32 SegIdx = static_cast<int32>(Op->GetNumberField(TEXT("SegmentIndex")));

	TArray<FAnimSegment>& Segs = Montage->SlotAnimTracks[SlotIdx].AnimTrack.AnimSegments;
	if (SegIdx < 0 || SegIdx >= Segs.Num())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("SegmentIndex %d out of range [0, %d)"), SegIdx, Segs.Num()));
		return;
	}

	Segs.RemoveAt(SegIdx);
	SyncMontageLength(Montage);
	Montage->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("SlotName"), SlotNameStr);
	Ctx.Entry->SetNumberField(TEXT("SegmentIndex"), SegIdx);
	Ctx.Entry->SetNumberField(TEXT("SequenceLength"), Montage->GetPlayLength());
}

static void HandleMontage_AddSection(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimMontage* Montage = MontageFrom(Ctx);
	const FString SectionName = FNexusArgs(Op).Str(TEXT("SectionName"));
	if (SectionName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddSection requires SectionName"));
		return;
	}

	for (const FCompositeSection& Sec : Montage->CompositeSections)
	{
		if (Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase))
		{
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Section '%s' already exists"), *SectionName));
			return;
		}
	}

	float SectionStartTime = 0.0f;
	if (Op->HasField(TEXT("SectionStartTime")))
	{
		SectionStartTime = static_cast<float>(Op->GetNumberField(TEXT("SectionStartTime")));
	}

	FCompositeSection NewSection;
	NewSection.SectionName = FName(*SectionName);
	// UE4 FCompositeSection 继承自 FAnimLinkableElement，通过 SetTime 写入时间
	NewSection.SetTime(SectionStartTime);

	const FString NextSectionName = FNexusArgs(Op).Str(TEXT("NextSectionName"));
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

	Ctx.Entry->SetStringField(TEXT("SectionName"),      SectionName);
	Ctx.Entry->SetNumberField(TEXT("SectionStartTime"), SectionStartTime);
	if (!NextSectionName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("NextSectionName"), NextSectionName);
	}
}

static void HandleMontage_RemoveSection(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimMontage* Montage = MontageFrom(Ctx);
	const FString SectionName = FNexusArgs(Op).Str(TEXT("SectionName"));
	if (SectionName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveSection requires SectionName"));
		return;
	}

	const int32 Removed = Montage->CompositeSections.RemoveAll([&SectionName](const FCompositeSection& Sec)
	{
		return Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase);
	});

	if (Removed == 0)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Section '%s' not found"), *SectionName));
	}
	else
	{
		Montage->MarkPackageDirty();
		Ctx.Entry->SetStringField(TEXT("SectionName"), SectionName);
	}
}

bool FNexusManageAssetAnimMontageCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UAnimMontage* Montage = FNexusAssetUtils::LoadAssetWithFallback<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		OutError = FString::Printf(TEXT("AnimMontage not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Montage;
	return true;
}

void FNexusManageAssetAnimMontageCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddSegment"),    &HandleMontage_AddSegment);
	OutHandlers.Add(TEXT("RemoveSegment"), &HandleMontage_RemoveSegment);
	OutHandlers.Add(TEXT("AddSection"),    &HandleMontage_AddSection);
	OutHandlers.Add(TEXT("RemoveSection"), &HandleMontage_RemoveSection);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetAnimMontageCapability)
