// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetBlendSpaceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "NexusMcpTool.h"

void FCreateAssetBlendSpaceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_blend_space");
	Out.Description = TEXT("Create BlendSpace (2D) or BlendSpace1D; configure axes/samples via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),       FNexusSchema::Str(TEXT("Asset path (package path)")))
		.Prop(TEXT("skeletonPath"),     FNexusSchema::Str(TEXT("Linked skeleton path")))
		.Prop(TEXT("blendSpaceType"),   FNexusSchema::Enum(TEXT("Type: blend_space (2D, default) or blend_space_1d"),
			{ TEXT("blend_space"), TEXT("blend_space_1d") }))
		.Required({ TEXT("assetPath"), TEXT("skeletonPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("locomotion"), TEXT("1d"), TEXT("2d"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("get_asset_blend_space"), TEXT("manage_asset_blend_space") };
	Out.WhenToUse = TEXT("Create BlendSpace; requires skeletonPath; configure via manage after create");
}

FCapabilityResult FCreateAssetBlendSpaceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		FString AssetPath, SkeletonPath, BsType;
		AssetPath    = A.Str(TEXT("assetPath"));
		SkeletonPath = A.Str(TEXT("skeletonPath"));
		Arguments->TryGetStringField(TEXT("blendSpaceType"), BsType);
		const bool b1D = BsType.Contains(TEXT("1d"), ESearchCase::IgnoreCase)
		               || BsType.Contains(TEXT("1D"), ESearchCase::CaseSensitive);

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			return;
		}

		if (LoadObject<UBlendSpace>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("BlendSpace already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);

		// UE 4.26/5.0 中 UBlendSpace1D 继承自 UBlendSpaceBase（不继承自 UBlendSpace）
		// UE 5.1+ 两者均继承自 UBlendSpace（BlendSpaceBase 合并）
		// UE 5.0 已废弃 UBlendSpaceBase 但仍存在，用 pragma 抑制废弃警告
		FString ActualType;
		UObject* BSRaw = nullptr;
		if (b1D)
		{
			BSRaw     = NewObject<UBlendSpace1D>(Package, *AssetName, RF_Public | RF_Standalone);
			ActualType = TEXT("BlendSpace1D");
		}
		else
		{
			BSRaw     = NewObject<UBlendSpace>(Package, *AssetName, RF_Public | RF_Standalone);
			ActualType = TEXT("BlendSpace");
		}

		if (!BSRaw) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("BlendSpace Createfailed")); return; }

		// 通过 UAnimationAsset 公共接口设置骨骼（BlendSpace 继承自 UAnimationAsset）
		if (UAnimationAsset* AnimAsset = Cast<UAnimationAsset>(BSRaw))
		{
			AnimAsset->SetSkeleton(Skeleton);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, BSRaw, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     BSRaw->GetName());
		Entry->SetStringField(TEXT("path"),     BSRaw->GetPathName());
		Entry->SetStringField(TEXT("assetType"), ActualType);
		Entry->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetBlendSpaceCapability)
