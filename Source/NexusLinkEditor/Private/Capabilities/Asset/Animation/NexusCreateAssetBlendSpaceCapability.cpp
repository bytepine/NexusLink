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

void FNexusCreateAssetBlendSpaceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetBlendSpace");
	Out.Description = TEXT("Create BlendSpace (2D) or BlendSpace1D; configure axes/samples via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),       FNexusSchema::Str(TEXT("Asset path (package path)")))
		.Prop(TEXT("SkeletonPath"),     FNexusSchema::Str(TEXT("Linked skeleton path")))
		.Prop(TEXT("BlendSpaceType"),   FNexusSchema::Enum(TEXT("Type: BlendSpace (2D, default) or BlendSpace1D"),
			{ TEXT("BlendSpace"), TEXT("BlendSpace1D") }))
		.Required({ TEXT("AssetPath"), TEXT("SkeletonPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("blend"), TEXT("locomotion"), TEXT("1d"), TEXT("2d"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("GetAssetBlendSpace"), TEXT("ManageAssetBlendSpace") };
	Out.WhenToUse = TEXT("Create BlendSpace; requires SkeletonPath; configure via manage after create");
}

FCapabilityResult FNexusCreateAssetBlendSpaceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		FString AssetPath, SkeletonPath, BsType;
		AssetPath    = A.Str(TEXT("AssetPath"));
		SkeletonPath = A.Str(TEXT("SkeletonPath"));
		BsType = A.Str(TEXT("BlendSpaceType"), BsType);
		const bool bIsBlendSpace1D = BsType.Contains(TEXT("1d"), ESearchCase::IgnoreCase)
		               || BsType.Contains(TEXT("1D"), ESearchCase::CaseSensitive);

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath));
			return;
		}

		UClass* BsClass = bIsBlendSpace1D ? UBlendSpace1D::StaticClass() : UBlendSpace::StaticClass();
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset(AssetPath, BsClass, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UObject* BSRaw = Created.Asset;
		FString ActualType = bIsBlendSpace1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace");

		// 通过 UAnimationAsset 公共接口设置骨骼（BlendSpace 继承自 UAnimationAsset）
		if (UAnimationAsset* AnimAsset = Cast<UAnimationAsset>(BSRaw))
		{
			AnimAsset->SetSkeleton(Skeleton);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(BSRaw->GetOutermost(), BSRaw, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(BSRaw);
		Entry->SetStringField(TEXT("AssetType"), ActualType);
		Entry->SetStringField(TEXT("Skeleton"), Skeleton->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetBlendSpaceCapability)
