// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Animation/AnimComposite.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetAnimComposite");
	Out.Description = TEXT("Create AnimComposite asset; add segments via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),    FNexusSchema::Str(TEXT("AnimComposite package path")))
		.Prop(TEXT("SkeletonPath"), FNexusSchema::Str(TEXT("Skeleton asset path (optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Composite"), TEXT("anim"), TEXT("Sequence"), TEXT("combine") };
	Out.RelatedCapabilities = { TEXT("GetAssetAnimComposite"), TEXT("ManageAssetAnimComposite") };
	Out.WhenToUse = TEXT("Create empty AnimComposite; bind skeleton when SkeletonPath given");
}

FCapabilityResult FNexusCreateAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UAnimComposite>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UAnimComposite* Composite = Cast<UAnimComposite>(Created.Asset);
		if (!Composite)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		if (Arguments->HasField(TEXT("SkeletonPath")))
		{
			const FString SkelPath = A.Str(TEXT("SkeletonPath"));
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkelPath);
			if (Skeleton) Composite->SetSkeleton(Skeleton);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Composite->GetOutermost(), Composite, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(Composite);
		Entry->SetNumberField(TEXT("SegmentCount"),  Composite->AnimationTrack.AnimSegments.Num());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetAnimCompositeCapability)
