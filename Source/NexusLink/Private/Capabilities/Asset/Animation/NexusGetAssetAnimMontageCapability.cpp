// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimMontageCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FGetAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_anim_montage");
	Out.Description = TEXT("检查 Montage 时间轴快照。只读，不触发播放。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("动画 Montage 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("montage"), TEXT("slot"), TEXT("segment"), TEXT("section"), TEXT("timeline")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_montage"), TEXT("create_asset_anim_montage"), TEXT("get_runtime_actor_animation") };
	Out.WhenToUse = TEXT("读 Montage 结构；运行时播放用 get_runtime_actor_animation");
}

FCapabilityResult FGetAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		UAnimMontage* Montage = FNexusAssetUtils::LoadAssetWithFallback<UAnimMontage>(AssetPath);
		if (!Montage)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimMontage 未找到: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),     Montage->GetName());
		Entry->SetNumberField(TEXT("duration"), Montage->GetPlayLength());

		// Skeleton 路径（继承自 UAnimSequenceBase）
		if (USkeleton* Skel = Montage->GetSkeleton())
		{
			Entry->SetStringField(TEXT("skeleton"), Skel->GetPathName());
		}

		// Slots 及其 Segments
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());

			TArray<TSharedPtr<FJsonValue>> Segments;
			for (const FAnimSegment& Seg : SlotTrack.AnimTrack.AnimSegments)
			{
			TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
			if (UAnimSequenceBase* AnimRef = Seg.GetAnimReference())
			{
				SegObj->SetStringField(TEXT("animSequence"), AnimRef->GetPathName());
			}
#else
			if (Seg.AnimReference)
			{
				SegObj->SetStringField(TEXT("animSequence"), Seg.AnimReference->GetPathName());
			}
#endif
				SegObj->SetNumberField(TEXT("startPos"),      Seg.StartPos);
				SegObj->SetNumberField(TEXT("animStartTime"), Seg.AnimStartTime);
				SegObj->SetNumberField(TEXT("animEndTime"),   Seg.AnimEndTime);
				SegObj->SetNumberField(TEXT("loopingCount"),  Seg.LoopingCount);
				Segments.Add(MakeShared<FJsonValueObject>(SegObj));
			}
		if (Segments.Num() > 0)
			{
				SlotObj->SetArrayField(TEXT("segments"), Segments);
			}
			Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
		Entry->SetArrayField(TEXT("slots"), Slots);

		// CompositeSections（命名时间段）
		TArray<TSharedPtr<FJsonValue>> Sections;
		for (const FCompositeSection& Sec : Montage->CompositeSections)
		{
			TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();
			SecObj->SetStringField(TEXT("name"), Sec.SectionName.ToString());
			// UE4/5 均通过 GetTime() 读取时间（FAnimLinkableElement 接口）
			SecObj->SetNumberField(TEXT("time"), Sec.GetTime());
			if (!Sec.NextSectionName.IsNone())
			{
				SecObj->SetStringField(TEXT("nextSection"), Sec.NextSectionName.ToString());
			}
			Sections.Add(MakeShared<FJsonValueObject>(SecObj));
		}
		Entry->SetArrayField(TEXT("sections"), Sections);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetAnimMontageCapability)
