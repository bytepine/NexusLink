// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimMontageCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("编辑 Montage 结构。增删槽位/片段/分段；须 save_asset。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),        FNexusSchema::Str(TEXT("动画 Montage 资产路径")))
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("操作类型"),
			{ TEXT("add_segment"), TEXT("remove_segment"), TEXT("add_section"), TEXT("remove_section") }))
		// add_segment
		.Prop(TEXT("animSequencePath"), FNexusSchema::Str(TEXT("AnimSequence 路径（add_segment）")))
		.Prop(TEXT("slotName"),         FNexusSchema::Str(TEXT("槽位名"), TEXT("DefaultSlot")))
		.Prop(TEXT("startPos"),         FNexusSchema::Num(TEXT("Montage 起始位置（秒）；默认追加到末尾")))
		.Prop(TEXT("animStartTime"),    FNexusSchema::Num(TEXT("动画内起始时间（秒）；默认 0")))
		.Prop(TEXT("animEndTime"),      FNexusSchema::Num(TEXT("动画内结束时间（秒）；默认全长")))
		// remove_segment
		.Prop(TEXT("segmentIndex"),     FNexusSchema::Int(TEXT("要删除的片段索引（remove_segment）"), TNumericLimits<int64>::Min(), 0))
		// add_section / remove_section
		.Prop(TEXT("sectionName"),      FNexusSchema::Str(TEXT("分段名（add_section / remove_section）")))
		.Prop(TEXT("sectionStartTime"), FNexusSchema::Num(TEXT("Montage 内分段起始时间（秒）（add_section）")))
		.Prop(TEXT("nextSectionName"),  FNexusSchema::Str(TEXT("循环下一分段（add_section，可选）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("montage"), TEXT("segment"), TEXT("section"), TEXT("slot"), TEXT("timeline")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_anim_montage"), TEXT("create_asset_anim_montage"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删 Montage 片段/分段/槽位");
}

FCapabilityResult FManageAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = Arguments->HasField(TEXT("assetPath")) ? Arguments->GetStringField(TEXT("assetPath")) : TEXT("");
		if (AssetPath.IsEmpty()) { OutError = TEXT("assetPath 为必填项"); return; }

		const FString Action = Arguments->HasField(TEXT("action")) ? Arguments->GetStringField(TEXT("action")).ToLower() : TEXT("");
		if (Action.IsEmpty()) { OutError = TEXT("缺少 action"); return; }

		UAnimMontage* Montage = FNexusAssetUtils::LoadAssetWithFallback<UAnimMontage>(AssetPath);
		if (!Montage) { OutError = FString::Printf(TEXT("AnimMontage 未找到: %s"), *AssetPath); return; }

		OutEntry->SetStringField(TEXT("action"), Action);

		if (Action == TEXT("add_segment"))
		{
			const FString SeqPath = Arguments->HasField(TEXT("animSequencePath")) ? Arguments->GetStringField(TEXT("animSequencePath")) : TEXT("");
			if (SeqPath.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("add_segment 需要 animSequencePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			UAnimSequenceBase* AnimSeq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequenceBase>(SeqPath);
			if (!AnimSeq)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimSequence 未找到: %s"), *SeqPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FString SlotNameStr = TEXT("DefaultSlot");
			Arguments->TryGetStringField(TEXT("slotName"), SlotNameStr);
			const FName SlotName(*SlotNameStr);

			const float FullLen       = AnimSeq->GetPlayLength();
			const float AnimStartTime = Arguments->HasField(TEXT("animStartTime")) ? (float)Arguments->GetNumberField(TEXT("animStartTime")) : 0.0f;
			const float AnimEndTime   = Arguments->HasField(TEXT("animEndTime"))   ? (float)Arguments->GetNumberField(TEXT("animEndTime"))   : FullLen;

			const int32 SlotIdx = FindOrCreateSlot(Montage, SlotName);
			FSlotAnimationTrack& Track = Montage->SlotAnimTracks[SlotIdx];

			// 未指定 startPos 时自动追加到 slot 末尾
			float StartPos = Arguments->HasField(TEXT("startPos"))
				? (float)Arguments->GetNumberField(TEXT("startPos"))
				: CalcSlotEndTime(Track);

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
			Montage->MarkPackageDirty();

			OutEntry->SetStringField(TEXT("slotName"),         SlotNameStr);
			OutEntry->SetStringField(TEXT("animSequencePath"), AnimSeq->GetPathName());
			OutEntry->SetNumberField(TEXT("startPos"),         StartPos);
			OutEntry->SetNumberField(TEXT("animStartTime"),    AnimStartTime);
			OutEntry->SetNumberField(TEXT("animEndTime"),      AnimEndTime);
			OutEntry->SetNumberField(TEXT("segmentIndex"),     Track.AnimTrack.AnimSegments.Num() - 1);	}
		else if (Action == TEXT("remove_segment"))
		{
			FString SlotNameStr = TEXT("DefaultSlot");
			Arguments->TryGetStringField(TEXT("slotName"), SlotNameStr);
			const FName SlotName(*SlotNameStr);

			int32 SlotIdx = INDEX_NONE;
			for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
			{
				if (Montage->SlotAnimTracks[i].SlotName == SlotName) { SlotIdx = i; break; }
			}
			if (SlotIdx == INDEX_NONE)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("槽位 '%s' 未找到"), *SlotNameStr));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			if (!Arguments->HasField(TEXT("segmentIndex")))
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("remove_segment 需要 segmentIndex"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			const int32 SegIdx = (int32)Arguments->GetNumberField(TEXT("segmentIndex"));

			TArray<FAnimSegment>& Segs = Montage->SlotAnimTracks[SlotIdx].AnimTrack.AnimSegments;
			if (SegIdx < 0 || SegIdx >= Segs.Num())
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("segmentIndex %d 超出范围 [0, %d)"), SegIdx, Segs.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			Segs.RemoveAt(SegIdx);
			Montage->MarkPackageDirty();
			OutEntry->SetStringField(TEXT("slotName"), SlotNameStr);
			OutEntry->SetNumberField(TEXT("segmentIndex"), SegIdx);
		}
		else if (Action == TEXT("add_section"))
		{
			FString SectionName;
			if (!Arguments->TryGetStringField(TEXT("sectionName"), SectionName) || SectionName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("add_section 需要 sectionName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			// 检查重名
			for (const FCompositeSection& Sec : Montage->CompositeSections)
			{
				if (Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase))
				{
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("分段 '%s' 已存在"), *SectionName));
					OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
					return;
				}
			}

			float SectionStartTime = 0.0f;
			if (Arguments->HasField(TEXT("sectionStartTime")))
			{
				SectionStartTime = (float)Arguments->GetNumberField(TEXT("sectionStartTime"));
			}

			FCompositeSection NewSection;
			NewSection.SectionName = FName(*SectionName);
			// UE4 FCompositeSection 继承自 FAnimLinkableElement，通过 SetTime 写入时间
			NewSection.SetTime(SectionStartTime);

			FString NextSectionName;
			if (Arguments->TryGetStringField(TEXT("nextSectionName"), NextSectionName) && !NextSectionName.IsEmpty())
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
			if (!Arguments->TryGetStringField(TEXT("sectionName"), SectionName) || SectionName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("remove_section 需要 sectionName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			const int32 Removed = Montage->CompositeSections.RemoveAll([&SectionName](const FCompositeSection& Sec)
			{
				return Sec.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase);
			});

			if (Removed == 0)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("分段 '%s' 未找到"), *SectionName));
			}
			else
			{
				Montage->MarkPackageDirty();
				OutEntry->SetStringField(TEXT("sectionName"), SectionName);
			}
		}
		else
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimMontageCapability)
