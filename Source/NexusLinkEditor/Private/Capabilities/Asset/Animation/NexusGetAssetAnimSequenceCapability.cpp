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

void FNexusGetAssetAnimSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetAnimSequence");
	Out.SearchAssetTypes = {TEXT("AnimSequence")};
	Out.Description = TEXT("Inspect AnimSequence snapshot. Writes via ManageAssetAnimSequence.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("AnimSequence asset path")))
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

		Entry->SetStringField(TEXT("name"), Seq->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("AnimSequence"));
		FNexusAssetUtils::AppendAnimSequenceMetadataFields(Seq, Entry);
		FNexusAssetUtils::AppendAnimSequenceNotifyFields(Seq, Entry);
		FNexusAssetUtils::AppendAnimSequenceCurveFields(Seq, Entry);

		if (const USkeleton* Skel = Seq->GetSkeleton())
		{
			Entry->SetStringField(TEXT("Skeleton"), Skel->GetPathName());
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetAnimSequenceCapability)
