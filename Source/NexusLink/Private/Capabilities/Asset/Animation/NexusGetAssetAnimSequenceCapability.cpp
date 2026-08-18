// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FGetAssetAnimSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_anim_sequence");
	Out.SearchAssetTypes = {TEXT("AnimSequence")};
	Out.Description = TEXT("Inspect AnimSequence snapshot. Writes via manage_asset_anim_sequence.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("AnimSequence asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("animation"), TEXT("clip"), TEXT("frame"), TEXT("skeleton"), TEXT("curve"), TEXT("keyframe") };
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_sequence"), TEXT("search_asset"), TEXT("get_asset_skeleton"), TEXT("get_asset_anim_montage"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("Read sequence metadata/notifies/float curves; use manage_asset_anim_sequence for writes");
}

FCapabilityResult FGetAssetAnimSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("assetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		UAnimSequence* Seq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(Path);
		if (!Seq)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Seq->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("AnimSequence"));
		FNexusAssetUtils::AppendAnimSequenceMetadataFields(Seq, Entry);
		FNexusAssetUtils::AppendAnimSequenceNotifyFields(Seq, Entry);
		FNexusAssetUtils::AppendAnimSequenceCurveFields(Seq, Entry);

		if (const USkeleton* Skel = Seq->GetSkeleton())
		{
			Entry->SetStringField(TEXT("skeleton"), Skel->GetPathName());
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetAnimSequenceCapability)
