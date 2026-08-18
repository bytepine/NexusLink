// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimCompositeCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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

FCapabilityResult FManageAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		UAnimComposite* Composite = LoadObject<UAnimComposite>(nullptr, *AssetPath);
		if (!Composite)
		{
			OutError = FString::Printf(TEXT("Failed to load AnimComposite: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>& Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			const FString Action = FNexusArgs(Op).Str(TEXT("action"));
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_segment"))
			{
				const FString AnimPath = FNexusArgs(Op).Str(TEXT("animPath"));
				UAnimSequenceBase* AnimRef = AnimPath.IsEmpty()
					? nullptr
					: LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);

				// 计算末尾 startPos
				float StartPos = 0.f;
				if (Op->HasField(TEXT("startPos")))
				{
					StartPos = (float)Op->GetNumberField(TEXT("startPos"));
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
				NewSeg.AnimStartTime =FNexusArgs(Op).Num(TEXT("animStartTime"), 0.f);
				NewSeg.AnimEndTime   =FNexusArgs(Op).Num(TEXT("animEndTime"), 0.f);
				NewSeg.AnimPlayRate  =FNexusArgs(Op).Num(TEXT("playRate"), 1.f);
				NewSeg.LoopingCount  = 1;

#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
				if (AnimRef) NewSeg.SetAnimReference(AnimRef, false);
#else
				NewSeg.AnimReference = AnimRef;
#endif
				const int32 NewIdx = Composite->AnimationTrack.AnimSegments.Add(NewSeg);
				Entry->SetNumberField(TEXT("newIndex"), NewIdx);
			}
			else if (Action == TEXT("remove_segment"))
			{
				if (!Op->HasField(TEXT("segmentIndex")))
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("remove_segment requires segmentIndex"));
					continue;
				}
				const int32 Idx = (int32)Op->GetNumberField(TEXT("segmentIndex"));
				if (!Composite->AnimationTrack.AnimSegments.IsValidIndex(Idx))
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
						FString::Printf(TEXT("segmentIndex %d out of bounds"), Idx));
					continue;
				}
				Composite->AnimationTrack.AnimSegments.RemoveAt(Idx);
			}
			else
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("Unknown action: %s"), *Action));
				continue;
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		Composite->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimCompositeCapability)
