// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimBlueprintCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FCreateAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_anim_blueprint");
	Out.Description = TEXT("Create ABP for skeleton, auto-linked; fill state machines via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("AnimBlueprint package path")))
		.Prop(TEXT("skeletonPath"), FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Required({ TEXT("assetPath"), TEXT("skeletonPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("abp"), TEXT("new"), TEXT("skeleton"), TEXT("animblueprint"), TEXT("rig") };
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_blueprint"), TEXT("get_asset_anim_blueprint") };
	Out.WhenToUse = TEXT("Create empty ABP; requires skeletonPath");
}

FCapabilityResult FCreateAssetAnimBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath    = A.Str(TEXT("assetPath"));
		const FString SkeletonPath = A.Str(TEXT("skeletonPath"));

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created = FNexusAssetUtils::CreateBlueprintAsset(
			AssetPath, TEXT("AnimInstance"), UAnimInstance::StaticClass(),
			UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass(), false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Created.Asset);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		AnimBP->TargetSkeleton = Skeleton;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		FNexusAssetUtils::NotifyCompileAndSave(AnimBP->GetOutermost(), AnimBP, AssetPath);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("name"),     AnimBP->GetName());
		OutEntry->SetStringField(TEXT("path"),     AnimBP->GetPathName());
		OutEntry->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAnimBlueprintCapability)

#endif // WITH_EDITOR
