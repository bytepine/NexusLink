// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimComposite.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FNexusGetAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetAnimComposite");
	Out.SearchAssetTypes = {TEXT("AnimComposite")};
	Out.Description = TEXT("Read AnimComposite track segments (AnimReference/StartPos/duration/PlayRate).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("AnimComposite asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Composite"), TEXT("anim"), TEXT("Segments"), TEXT("track") };
	Out.RelatedCapabilities = { TEXT("CreateAssetAnimComposite"), TEXT("ManageAssetAnimComposite") };
}

FCapabilityResult FNexusGetAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		UAnimComposite* Composite = LoadObject<UAnimComposite>(nullptr, *AssetPath);
		if (!Composite)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load AnimComposite: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Composite->GetName());
		Entry->SetStringField(TEXT("Path"), Composite->GetPathName());

		if (USkeleton* Skel = Composite->GetSkeleton())
			Entry->SetStringField(TEXT("Skeleton"), Skel->GetPathName());

		TArray<TSharedPtr<FJsonValue>> SegsArr;
		const TArray<FAnimSegment>& Segments = Composite->AnimationTrack.AnimSegments;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			const FAnimSegment& Seg = Segments[SegmentIndex];
			TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
			SegObj->SetNumberField(TEXT("Index"), SegmentIndex);
			SegObj->SetNumberField(TEXT("StartPos"), Seg.StartPos);
			SegObj->SetNumberField(TEXT("AnimStartTime"), Seg.AnimStartTime);
			SegObj->SetNumberField(TEXT("AnimEndTime"), Seg.AnimEndTime);
			SegObj->SetNumberField(TEXT("PlayRate"), Seg.AnimPlayRate);
			SegObj->SetNumberField(TEXT("LoopCount"), Seg.LoopingCount);

#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
			UAnimSequenceBase* AnimRef = Seg.GetAnimReference();
#else
			UAnimSequenceBase* AnimRef = Seg.AnimReference;
#endif
			if (AnimRef)
				SegObj->SetStringField(TEXT("AnimReference"), AnimRef->GetPathName());

			SegsArr.Add(MakeShared<FJsonValueObject>(SegObj));
		}

		Entry->SetNumberField(TEXT("SegmentCount"), Segments.Num());
		Entry->SetArrayField(TEXT("Segments"), SegsArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetAnimCompositeCapability)
