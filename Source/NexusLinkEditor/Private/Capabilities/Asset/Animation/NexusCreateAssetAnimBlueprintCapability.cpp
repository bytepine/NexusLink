// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimBlueprintCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetAnimBlueprint");
	Out.Description = TEXT("Create ABP for skeleton, auto-linked; fill state machines via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),    FNexusSchema::Str(TEXT("AnimBlueprint package path")))
		.Prop(TEXT("SkeletonPath"), FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Required({ TEXT("AssetPath"), TEXT("SkeletonPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("abp"), TEXT("new"), TEXT("Skeleton"), TEXT("animblueprint"), TEXT("rig") };
	Out.RelatedCapabilities = { TEXT("ManageAssetAnimBlueprint"), TEXT("GetAssetAnimBlueprint") };
	Out.WhenToUse = TEXT("Create empty ABP; requires SkeletonPath");
}

FCapabilityResult FNexusCreateAssetAnimBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath    = A.Str(TEXT("AssetPath"));
		const FString SkeletonPath = A.Str(TEXT("SkeletonPath"));

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FNexusAssetEditorUtils::FAssetCreateOutcome Created = FNexusAssetEditorUtils::CreateBlueprintAsset(
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
		FNexusAssetEditorUtils::NotifyCompileAndSave(AnimBP->GetOutermost(), AnimBP, AssetPath);

		TSharedPtr<FJsonObject> OutEntry = FNexusCapabilityResultBuilder::MakeCreatedEntry(AnimBP);
		OutEntry->SetStringField(TEXT("Skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetAnimBlueprintCapability)

#endif // WITH_EDITOR
