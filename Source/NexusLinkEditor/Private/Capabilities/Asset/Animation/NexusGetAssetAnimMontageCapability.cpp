// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimMontageCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FNexusGetAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetAnimMontage");
	Out.SearchAssetTypes = {TEXT("AnimMontage")};
	Out.Description = TEXT("Inspect Montage timeline snapshot. Read-only; no playback.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("AnimMontage asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("Montage"), TEXT("slot"), TEXT("segment"), TEXT("Section"), TEXT("timeline")
	};
	Out.RelatedCapabilities = { TEXT("ManageAssetAnimMontage"), TEXT("CreateAssetAnimMontage"), TEXT("GetRuntimeActorAnimation") };
	Out.WhenToUse = TEXT("Read Montage structure; runtime playback via GetRuntimeActorAnimation");
}

FCapabilityResult FNexusGetAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		UAnimMontage* Montage = FNexusAssetUtils::LoadAssetWithFallback<UAnimMontage>(AssetPath);
		if (!Montage)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("AnimMontage not found: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),     Montage->GetName());
		Entry->SetNumberField(TEXT("Duration"), Montage->GetPlayLength());

		// Skeleton 路径（继承自 UAnimSequenceBase）
		if (USkeleton* Skel = Montage->GetSkeleton())
		{
			Entry->SetStringField(TEXT("Skeleton"), Skel->GetPathName());
		}

		// Slots 及其 Segments
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetStringField(TEXT("SlotName"), SlotTrack.SlotName.ToString());

			TArray<TSharedPtr<FJsonValue>> Segments;
			for (const FAnimSegment& Seg : SlotTrack.AnimTrack.AnimSegments)
			{
			TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
			if (UAnimSequenceBase* AnimRef = Seg.GetAnimReference())
			{
				SegObj->SetStringField(TEXT("AnimSequence"), AnimRef->GetPathName());
			}
#else
			if (Seg.AnimReference)
			{
				SegObj->SetStringField(TEXT("AnimSequence"), Seg.AnimReference->GetPathName());
			}
#endif
				SegObj->SetNumberField(TEXT("StartPos"),      Seg.StartPos);
				SegObj->SetNumberField(TEXT("AnimStartTime"), Seg.AnimStartTime);
				SegObj->SetNumberField(TEXT("AnimEndTime"),   Seg.AnimEndTime);
				SegObj->SetNumberField(TEXT("LoopingCount"),  Seg.LoopingCount);
				Segments.Add(MakeShared<FJsonValueObject>(SegObj));
			}
		if (Segments.Num() > 0)
			{
				SlotObj->SetArrayField(TEXT("Segments"), Segments);
			}
			Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
		Entry->SetArrayField(TEXT("Slots"), Slots);

		// CompositeSections（命名时间段）
		TArray<TSharedPtr<FJsonValue>> Sections;
		for (const FCompositeSection& Sec : Montage->CompositeSections)
		{
			TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();
			SecObj->SetStringField(TEXT("name"), Sec.SectionName.ToString());
			// UE4/5 均通过 GetTime() 读取时间（FAnimLinkableElement 接口）
			SecObj->SetNumberField(TEXT("Time"), Sec.GetTime());
			if (!Sec.NextSectionName.IsNone())
			{
				SecObj->SetStringField(TEXT("NextSection"), Sec.NextSectionName.ToString());
			}
			Sections.Add(MakeShared<FJsonValueObject>(SecObj));
		}
		Entry->SetArrayField(TEXT("Sections"), Sections);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetAnimMontageCapability)
