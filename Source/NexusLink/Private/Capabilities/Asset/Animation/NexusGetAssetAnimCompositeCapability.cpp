// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimComposite.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FGetAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_anim_composite");
	Out.SearchAssetTypes = {TEXT("AnimComposite")};
	Out.Description = TEXT("读取 AnimComposite 合成轨道中的片段列表（animReference/startPos/duration/playRate）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("AnimComposite 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("composite"), TEXT("anim"), TEXT("segments"), TEXT("track") };
	Out.RelatedCapabilities = { TEXT("create_asset_anim_composite"), TEXT("manage_asset_anim_composite") };
}

FCapabilityResult FGetAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UAnimComposite* Composite = LoadObject<UAnimComposite>(nullptr, *AssetPath);
		if (!Composite)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载 AnimComposite 失败: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Composite->GetName());
		Entry->SetStringField(TEXT("path"), Composite->GetPathName());

		if (USkeleton* Skel = Composite->GetSkeleton())
			Entry->SetStringField(TEXT("skeleton"), Skel->GetPathName());

		TArray<TSharedPtr<FJsonValue>> SegsArr;
		const TArray<FAnimSegment>& Segments = Composite->AnimationTrack.AnimSegments;
		for (int32 i = 0; i < Segments.Num(); ++i)
		{
			const FAnimSegment& Seg = Segments[i];
			TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
			SegObj->SetNumberField(TEXT("index"), i);
			SegObj->SetNumberField(TEXT("startPos"), Seg.StartPos);
			SegObj->SetNumberField(TEXT("animStartTime"), Seg.AnimStartTime);
			SegObj->SetNumberField(TEXT("animEndTime"), Seg.AnimEndTime);
			SegObj->SetNumberField(TEXT("playRate"), Seg.AnimPlayRate);
			SegObj->SetNumberField(TEXT("loopCount"), Seg.LoopingCount);

#if NX_UE_HAS_ANIM_SEGMENT_ACCESSOR
			UAnimSequenceBase* AnimRef = Seg.GetAnimReference();
#else
			UAnimSequenceBase* AnimRef = Seg.AnimReference;
#endif
			if (AnimRef)
				SegObj->SetStringField(TEXT("animReference"), AnimRef->GetPathName());

			SegsArr.Add(MakeShared<FJsonValueObject>(SegObj));
		}

		Entry->SetNumberField(TEXT("segmentCount"), Segments.Num());
		Entry->SetArrayField(TEXT("segments"), SegsArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetAnimCompositeCapability)
