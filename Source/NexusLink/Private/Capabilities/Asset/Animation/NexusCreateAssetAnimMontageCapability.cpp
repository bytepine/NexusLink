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

void FCreateAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_anim_montage");
	Out.Description = TEXT("Create Montage for skeleton; add segments via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("Montage package path")))
		.Prop(TEXT("skeletonPath"), FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Required({ TEXT("assetPath"), TEXT("skeletonPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("montage"), TEXT("new"), TEXT("skeleton"), TEXT("sequence"), TEXT("rig") };
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_montage"), TEXT("get_asset_anim_montage") };
	Out.WhenToUse = TEXT("Create empty Montage; requires skeletonPath");
}

FCapabilityResult FCreateAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();


		const FString AssetPath    = A.Str(TEXT("assetPath"));
		const FString SkeletonPath = A.Str(TEXT("skeletonPath"));

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			return;
		}

		if (LoadObject<UAnimMontage>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("AnimMontage already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UAnimMontage* Montage = NewObject<UAnimMontage>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Montage) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("AnimMontage Createfailed")); return; }

		Montage->SetSkeleton(Skeleton);
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Montage, AssetPath);

		OutEntry->SetStringField(TEXT("name"),     Montage->GetName());
		OutEntry->SetStringField(TEXT("path"),     Montage->GetPathName());
		OutEntry->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAnimMontageCapability)
