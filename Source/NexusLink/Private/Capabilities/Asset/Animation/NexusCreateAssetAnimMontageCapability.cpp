// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimMontageCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetAnimMontageCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_anim_montage");
	Out.Description = TEXT("为骨骼创建 Montage；用 manage 添加片段。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("Montage 包路径")))
		.Prop(TEXT("skeletonPath"), FNexusSchema::Str(TEXT("骨骼资产路径")))
		.Required({ TEXT("assetPath"), TEXT("skeletonPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("montage"), TEXT("new"), TEXT("skeleton"), TEXT("sequence"), TEXT("rig") };
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_montage"), TEXT("get_asset_anim_montage") };
	Out.WhenToUse = TEXT("创建空白 Montage；需要 skeletonPath");
}

FCapabilityResult FCreateAssetAnimMontageCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("加载 Skeleton 失败: %s"), *SkeletonPath));
			return;
		}

		if (LoadObject<UAnimMontage>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("AnimMontage already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UAnimMontage* Montage = NewObject<UAnimMontage>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Montage) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("AnimMontage 创建失败")); return; }

		Montage->SetSkeleton(Skeleton);
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Montage, AssetPath);

		OutEntry->SetStringField(TEXT("name"),     Montage->GetName());
		OutEntry->SetStringField(TEXT("path"),     Montage->GetPathName());
		OutEntry->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		OutEntry->SetBoolField(TEXT("success"),    true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAnimMontageCapability)
