// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimSequenceBase.h"
#include "NexusMcpTool.h"

void FManageAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_anim_composite");
	Out.SearchAssetTypes = {TEXT("AnimComposite")};
	Out.Description = TEXT("编辑 AnimComposite 合成轨道片段。operations[].action: add_segment / remove_segment。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"),        FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("add_segment"), TEXT("remove_segment") }))
		.Prop(TEXT("animPath"),          FNexusSchema::Str(TEXT("AnimSequence 路径（add_segment）")))
		.Prop(TEXT("startPos"),          FNexusSchema::Num(TEXT("片段起始时间（add_segment，默认末尾）")))
		.Prop(TEXT("animStartTime"),     FNexusSchema::Num(TEXT("源动画起始（add_segment，默认0）")))
		.Prop(TEXT("animEndTime"),       FNexusSchema::Num(TEXT("源动画结束（add_segment，0=全长）")))
		.Prop(TEXT("playRate"),          FNexusSchema::Num(TEXT("播放速率（add_segment，默认1.0）")))
		.Prop(TEXT("segmentIndex"),      FNexusSchema::Int(TEXT("片段索引（remove_segment）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("AnimComposite 资产路径")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("composite"), TEXT("segment"), TEXT("add"), TEXT("remove"), TEXT("track") };
	Out.RelatedCapabilities = { TEXT("create_asset_anim_composite"), TEXT("get_asset_anim_composite") };
}

FCapabilityResult FManageAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")) || !Arguments->HasField(TEXT("operations")))
		{
			OutError = TEXT("缺少 assetPath 或 operations");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UAnimComposite* Composite = LoadObject<UAnimComposite>(nullptr, *AssetPath);
		if (!Composite)
		{
			OutError = FString::Printf(TEXT("加载 AnimComposite 失败: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>& OpsArr = Arguments->GetArrayField(TEXT("operations"));
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>& Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			const FString Action = Op->HasField(TEXT("action")) ? Op->GetStringField(TEXT("action")) : TEXT("");
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_segment"))
			{
				const FString AnimPath = Op->HasField(TEXT("animPath")) ? Op->GetStringField(TEXT("animPath")) : TEXT("");
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
				NewSeg.AnimStartTime = Op->HasField(TEXT("animStartTime")) ? (float)Op->GetNumberField(TEXT("animStartTime")) : 0.f;
				NewSeg.AnimEndTime   = Op->HasField(TEXT("animEndTime"))   ? (float)Op->GetNumberField(TEXT("animEndTime"))   : 0.f;
				NewSeg.AnimPlayRate  = Op->HasField(TEXT("playRate"))      ? (float)Op->GetNumberField(TEXT("playRate"))      : 1.f;
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
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("remove_segment 需要 segmentIndex"));
					continue;
				}
				const int32 Idx = (int32)Op->GetNumberField(TEXT("segmentIndex"));
				if (!Composite->AnimationTrack.AnimSegments.IsValidIndex(Idx))
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
						FString::Printf(TEXT("segmentIndex %d 越界"), Idx));
					continue;
				}
				Composite->AnimationTrack.AnimSegments.RemoveAt(Idx);
			}
			else
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("未知 action: %s"), *Action));
				continue;
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		Composite->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimCompositeCapability)
