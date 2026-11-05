// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"

void FNexusGetAssetAnimSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetAnimSequence");
	Out.SearchAssetTypes = {TEXT("AnimSequence")};
	Out.Description = TEXT("Inspect AnimSequence snapshot. Writes via ManageAssetAnimSequence.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("AnimSequence asset path")))
		.Prop(TEXT("BoneNames"), FNexusSchema::StrArr(TEXT("Optional 1-32 skeleton bone names to sample in local space")))
		.Prop(TEXT("SampleTimes"), FNexusSchema::ArrayOf(TEXT("1-128 times in seconds within the clip; required with BoneNames"), FNexusSchema::Num(TEXT("Time in seconds"))))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Sequence"), TEXT("Animation"), TEXT("clip"), TEXT("frame"), TEXT("Skeleton"), TEXT("curve"), TEXT("keyframe") };
	Out.RelatedCapabilities = { TEXT("ManageAssetAnimSequence"), TEXT("SearchAsset"), TEXT("GetAssetSkeleton"), TEXT("GetAssetAnimMontage"), TEXT("GetAssetRefs") };
	Out.WhenToUse = TEXT("Read sequence metadata/notifies/float curves; use ManageAssetAnimSequence for writes");
}

FCapabilityResult FNexusGetAssetAnimSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		UAnimSequence* Seq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(Path);
		if (!Seq)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("Name"), Seq->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("AnimSequence"));
		FNexusAssetUtils::AppendAnimSequenceMetadataFields(Seq, Entry);
		FNexusAssetUtils::AppendAnimSequenceNotifyFields(Seq, Entry);
		FNexusAssetUtils::AppendAnimSequenceCurveFields(Seq, Entry);

		if (const USkeleton* Skel = Seq->GetSkeleton())
		{
			Entry->SetStringField(TEXT("Skeleton"), Skel->GetPathName());
		}
		if (Arguments->HasField(TEXT("BoneNames")) || Arguments->HasField(TEXT("SampleTimes")))
		{
			const auto Names = A.StrArr(TEXT("BoneNames"));
			const TArray<TSharedPtr<FJsonValue>>* Times = nullptr;
			const USkeleton* Skeleton = Seq->GetSkeleton();
			if (!Skeleton || Names.Num() < 1 || Names.Num() > 32 || !Arguments->TryGetArrayField(TEXT("SampleTimes"), Times) || Times->Num() < 1 || Times->Num() > 128)
			{ OutError = TEXT("Sampling requires a skeleton, 1-32 BoneNames and 1-128 SampleTimes"); return; }
			for (const auto& Time : *Times)
				if (!Time || Time->Type != EJson::Number || !FMath::IsFinite(Time->AsNumber()) || Time->AsNumber() < 0 || Time->AsNumber() > Seq->GetPlayLength())
				{ OutError = TEXT("SampleTimes must be finite seconds within the animation clip"); return; }
			const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
			for (const auto& Name : Names) if (Reference.FindBoneIndex(FName(*Name)) == INDEX_NONE) { OutError = TEXT("Unknown skeleton bone: ") + Name; return; }
			TArray<TSharedPtr<FJsonValue>> Samples;
			for (const auto& Name : Names) for (const auto& Time : *Times)
			{
				const int32 BoneIndex = Reference.FindBoneIndex(FName(*Name));
				FTransform Transform = Reference.GetRefBonePose()[BoneIndex];
#if NX_UE_HAS_ANIM_BONE_EXTRACT_CONTEXT
				Seq->GetBoneTransform(Transform, FSkeletonPoseBoneIndex(BoneIndex), FAnimExtractContext(Time->AsNumber()), true);
#elif NX_UE_HAS_ANIM_SKELETON_BONE_TRANSFORM
				Seq->GetBoneTransform(Transform, FSkeletonPoseBoneIndex(BoneIndex), Time->AsNumber(), true);
#else
				OutError = TEXT("Bone pose sampling requires UE 5.2+; metadata remains available"); return;
#endif
				auto Sample = MakeShared<FJsonObject>(); Sample->SetStringField(TEXT("BoneName"), Name); Sample->SetNumberField(TEXT("TimeSeconds"), Time->AsNumber());
				const FVector Position = Transform.GetTranslation(), Scale = Transform.GetScale3D(); const FQuat Rotation = Transform.GetRotation();
				TArray<TSharedPtr<FJsonValue>> Translation, RotationValues, ScaleValues;
				for (double Value : {static_cast<double>(Position.X), static_cast<double>(Position.Y), static_cast<double>(Position.Z)}) Translation.Add(MakeShared<FJsonValueNumber>(Value));
				for (double Value : {static_cast<double>(Rotation.X), static_cast<double>(Rotation.Y), static_cast<double>(Rotation.Z), static_cast<double>(Rotation.W)}) RotationValues.Add(MakeShared<FJsonValueNumber>(Value));
				for (double Value : {static_cast<double>(Scale.X), static_cast<double>(Scale.Y), static_cast<double>(Scale.Z)}) ScaleValues.Add(MakeShared<FJsonValueNumber>(Value));
				Sample->SetArrayField(TEXT("Translation"), Translation); Sample->SetArrayField(TEXT("RotationQuaternion"), RotationValues); Sample->SetArrayField(TEXT("Scale"), ScaleValues);
				Samples.Add(MakeShared<FJsonValueObject>(Sample));
			}
			Entry->SetStringField(TEXT("SampleSpace"), TEXT("Local")); Entry->SetArrayField(TEXT("BoneSamples"), Samples);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetAnimSequenceCapability)
