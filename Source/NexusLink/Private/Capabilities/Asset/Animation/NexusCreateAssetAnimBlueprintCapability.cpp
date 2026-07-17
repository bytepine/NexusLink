// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimBlueprintCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_anim_blueprint");
	Out.Description = TEXT("为骨骼创建 ABP，自动关联；用 manage 填充状态机。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("动画蓝图包路径")))
		.Prop(TEXT("skeletonPath"), FNexusSchema::Str(TEXT("骨骼资产路径")))
		.Required({ TEXT("assetPath"), TEXT("skeletonPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("abp"), TEXT("new"), TEXT("skeleton"), TEXT("animblueprint"), TEXT("rig") };
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_blueprint"), TEXT("get_asset_anim_blueprint") };
	Out.WhenToUse = TEXT("创建空白 ABP；需要 skeletonPath");
}

FCapabilityResult FCreateAssetAnimBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")) || !Arguments->HasField(TEXT("skeletonPath")))
		{
			OutError = TEXT("缺少 assetPath 或 skeletonPath");
			return;
		}

		const FString AssetPath    = Arguments->GetStringField(TEXT("assetPath"));
		const FString SkeletonPath = Arguments->GetStringField(TEXT("skeletonPath"));
		const FString AssetName    = FPaths::GetBaseFilename(AssetPath);

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("加载 Skeleton 失败: %s"), *SkeletonPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (LoadObject<UAnimBlueprint>(nullptr, *AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint 已存在: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(), Package, *AssetName,
			BPTYPE_Normal, UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass()
		));
		if (!AnimBP) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("AnimBlueprint 创建失败")); return; }

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		AnimBP->TargetSkeleton = Skeleton;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		FNexusAssetUtils::NotifyCompileAndSave(Package, AnimBP, AssetPath);

		OutEntry->SetStringField(TEXT("name"),     AnimBP->GetName());
		OutEntry->SetStringField(TEXT("path"),     AnimBP->GetPathName());
		OutEntry->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAnimBlueprintCapability)

#endif // WITH_EDITOR
