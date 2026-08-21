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
		BsType = A.Str(TEXT("blendSpaceType"), BsType);
		const bool b1D = BsType.Contains(TEXT("1d"), ESearchCase::IgnoreCase)
		               || BsType.Contains(TEXT("1D"), ESearchCase::CaseSensitive);

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			return;
		}

		UClass* BsClass = b1D ? UBlendSpace1D::StaticClass() : UBlendSpace::StaticClass();
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset(AssetPath, BsClass, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UObject* BSRaw = Created.Asset;
		FString ActualType = b1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace");

		// 通过 UAnimationAsset 公共接口设置骨骼（BlendSpace 继承自 UAnimationAsset）
		if (UAnimationAsset* AnimAsset = Cast<UAnimationAsset>(BSRaw))
		{
			AnimAsset->SetSkeleton(Skeleton);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(BSRaw->GetOutermost(), BSRaw, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     BSRaw->GetName());
		Entry->SetStringField(TEXT("path"),     BSRaw->GetPathName());
		Entry->SetStringField(TEXT("assetType"), ActualType);
		Entry->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetBlendSpaceCapability)
