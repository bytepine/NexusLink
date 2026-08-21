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

void FCreateAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_anim_composite");
	Out.Description = TEXT("Create AnimComposite asset; add segments via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("AnimComposite package path")))
		.Prop(TEXT("skeletonPath"), FNexusSchema::Str(TEXT("Skeleton asset path (optional)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("composite"), TEXT("anim"), TEXT("sequence"), TEXT("combine") };
	Out.RelatedCapabilities = { TEXT("get_asset_anim_composite"), TEXT("manage_asset_anim_composite") };
	Out.WhenToUse = TEXT("Create empty AnimComposite; bind skeleton when skeletonPath given");
}

FCapabilityResult FCreateAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

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
		if (Arguments->HasField(TEXT("skeletonPath")))
		{
			const FString SkelPath = A.Str(TEXT("skeletonPath"));
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkelPath);
			if (Skeleton) Composite->SetSkeleton(Skeleton);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Composite->GetOutermost(), Composite, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),          Composite->GetName());
		Entry->SetStringField(TEXT("path"),          Composite->GetPathName());
		Entry->SetNumberField(TEXT("segmentCount"),  Composite->AnimationTrack.AnimSegments.Num());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAnimCompositeCapability)
