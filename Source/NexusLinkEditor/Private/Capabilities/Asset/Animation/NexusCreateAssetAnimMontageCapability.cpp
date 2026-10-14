// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimMontageCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetAnimMontage");
	Out.Description = TEXT("Create Montage for skeleton; add segments via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),    FNexusSchema::Str(TEXT("Montage package path")))
		.Prop(TEXT("SkeletonPath"), FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Required({ TEXT("AssetPath"), TEXT("SkeletonPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Montage"), TEXT("new"), TEXT("Skeleton"), TEXT("Sequence"), TEXT("rig") };
	Out.RelatedCapabilities = { TEXT("ManageAssetAnimMontage"), TEXT("GetAssetAnimMontage") };
	Out.WhenToUse = TEXT("Create empty Montage; requires SkeletonPath");
}

FCapabilityResult FNexusCreateAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath    = A.Str(TEXT("AssetPath"));
		const FString SkeletonPath = A.Str(TEXT("SkeletonPath"));

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UAnimMontage>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UAnimMontage* Montage = Cast<UAnimMontage>(Created.Asset);
		if (!Montage)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		Montage->SetSkeleton(Skeleton);
		FNexusAssetUtils::NotifyAndSaveCreated(Montage->GetOutermost(), Montage, AssetPath);

		TSharedPtr<FJsonObject> OutEntry = FNexusCapabilityResultBuilder::MakeCreatedEntry(Montage);
		OutEntry->SetStringField(TEXT("Skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetAnimMontageCapability)
