// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimMontageCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

		UAnimMontage* Montage = FNexusAssetUtils::LoadAssetWithFallback<UAnimMontage>(AssetPath);
		if (!Montage) { OutError = FString::Printf(TEXT("AnimMontage not found: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0) { OutError = TEXT("Missing or empty operations"); return; }

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("path"), AssetPath);

		const FString Action = FNexusArgs(OpArgs).Str(TEXT("action")).ToLower();
		if (Action.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("Missing action"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			continue;
		}

		OutEntry->SetStringField(TEXT("action"), Action);

		if (Action == TEXT("add_segment"))
		{
			const FString SeqPath = FNexusArgs(OpArgs).Str(TEXT("animSequencePath"));
			if (SeqPath.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("add_segment requires animSequencePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			UAnimSequenceBase* AnimSeq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequenceBase>(SeqPath);
			if (!AnimSeq)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimSequence not found: %s"), *SeqPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			FString SlotNameStr = TEXT("DefaultSlot");
			OpArgs->TryGetStringField(TEXT("slotName"), SlotNameStr);
			const FName SlotName(*SlotNameStr);

			const float FullLen       = AnimSeq->GetPlayLength();
			const float AnimStartTime =FNexusArgs(OpArgs).Num(TEXT("animStartTime"), 0.0f);
			const float AnimEndTime   =FNexusArgs(OpArgs).Num(TEXT("animEndTime"), FullLen);

			const int32 SlotIdx = FindOrCreateSlot(Montage, SlotName);
			FSlotAnimationTrack& Track = Montage->SlotAnimTracks[SlotIdx];

			// 未指定 startPos 时自动追加到 slot 末尾
			float StartPos =FNexusArgs(OpArgs).Num(TEXT("startPos"), CalcSlotEndTime(Track));

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
			// UE5：SetCompositeLength；UE4 Editor：SetSequenceLength；UE4 Game：直写 SequenceLength
			const float NewLen = Montage->CalculateSequenceLength();
#if NX_UE_HAS_ANIM_COMPOSITE_SET_LENGTH
			Montage->SetCompositeLength(NewLen);
#elif WITH_EDITOR
			Montage->SetSequenceLength(NewLen);
#else
			Montage->SequenceLength = NewLen;
#endif
			Montage->MarkPackageDirty();

			OutEntry->SetStringField(TEXT("slotName"),         SlotNameStr);
			OutEntry->SetStringField(TEXT("animSequencePath"), AnimSeq->GetPathName());
			OutEntry->SetNumberField(TEXT("startPos"),         StartPos);
			OutEntry->SetNumberField(TEXT("animStartTime"),    AnimStartTime);
			OutEntry->SetNumberField(TEXT("animEndTime"),      AnimEndTime);
			OutEntry->SetNumberField(TEXT("segmentIndex"),     Track.AnimTrack.AnimSegments.Num() - 1);
			OutEntry->SetNumberField(TEXT("sequenceLength"),   Montage->GetPlayLength());
		}
		else if (Action == TEXT("remove_segment"))
		{
			FString SlotNameStr = TEXT("DefaultSlot");
			OpArgs->TryGetStringField(TEXT("slotName"), SlotNameStr);
			const FName SlotName(*SlotNameStr);

			int32 SlotIdx = INDEX_NONE;
			for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
			{
				if (Montage->SlotAnimTracks[i].SlotName == SlotName) { SlotIdx = i; break; }
			}
			if (SlotIdx == INDEX_NONE)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Slot '%s' not found"), *SlotNameStr));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			if (!OpArgs->HasField(TEXT("segmentIndex")))
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("remove_segment requires segmentIndex"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}
			const int32 SegIdx = (int32)OpArgs->GetNumberField(TEXT("segmentIndex"));

			TArray<FAnimSegment>& Segs = Montage->SlotAnimTracks[SlotIdx].AnimTrack.AnimSegments;
			if (SegIdx < 0 || SegIdx >= Segs.Num())
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("segmentIndex %d out of range [0, %d)"), SegIdx, Segs.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			Segs.RemoveAt(SegIdx);
			const float NewLen = Montage->CalculateSequenceLength();
#if NX_UE_HAS_ANIM_COMPOSITE_SET_LENGTH
			Montage->SetCompositeLength(NewLen);
#elif WITH_EDITOR
			Montage->SetSequenceLength(NewLen);
#else
			Montage->SequenceLength = NewLen;
#endif
			Montage->MarkPackageDirty();
			OutEntry->SetStringField(TEXT("slotName"), SlotNameStr);
			OutEntry->SetNumberField(TEXT("segmentIndex"), SegIdx);
			OutEntry->SetNumberField(TEXT("sequenceLength"), Montage->GetPlayLength());
		}
		else if (Action == TEXT("add_section"))
		{
			FString SectionName;
			if (!OpArgs->TryGetStringField(TEXT("sectionName"), SectionName) || SectionName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("add_section requires sectionName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			// 检查重名
			for (const FCompositeSection& Sec : Montage->CompositeSections)
			{
				if (Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase))
				{
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Section '%s' already exists"), *SectionName));
					OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
					continue;
				}
			}

			float SectionStartTime = 0.0f;
			if (OpArgs->HasField(TEXT("sectionStartTime")))
			{
				SectionStartTime = (float)OpArgs->GetNumberField(TEXT("sectionStartTime"));
			}

			FCompositeSection NewSection;
			NewSection.SectionName = FName(*SectionName);
			// UE4 FCompositeSection 继承自 FAnimLinkableElement，通过 SetTime 写入时间
			NewSection.SetTime(SectionStartTime);

			FString NextSectionName;
			if (OpArgs->TryGetStringField(TEXT("nextSectionName"), NextSectionName) && !NextSectionName.IsEmpty())
			{
				NewSection.NextSectionName = FName(*NextSectionName);
			}

			Montage->CompositeSections.Add(NewSection);
			// 按时间排序，保持 CompositeSections 顺序与时间轴一致
			Montage->CompositeSections.Sort([](const FCompositeSection& A, const FCompositeSection& B)
			{
				return A.GetTime() < B.GetTime();
			});
			Montage->MarkPackageDirty();

			OutEntry->SetStringField(TEXT("sectionName"),      SectionName);
			OutEntry->SetNumberField(TEXT("sectionStartTime"), SectionStartTime);
			if (!NextSectionName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("nextSectionName"), NextSectionName);
			}
		}
		else if (Action == TEXT("remove_section"))
		{
			FString SectionName;
			if (!OpArgs->TryGetStringField(TEXT("sectionName"), SectionName) || SectionName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("remove_section requires sectionName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const int32 Removed = Montage->CompositeSections.RemoveAll([&SectionName](const FCompositeSection& Sec)
			{
				return Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase);
			});

			if (Removed == 0)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Section '%s' not found"), *SectionName));
			}
			else
			{
				Montage->MarkPackageDirty();
				OutEntry->SetStringField(TEXT("sectionName"), SectionName);
			}
		}
		else
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimMontageCapability)
