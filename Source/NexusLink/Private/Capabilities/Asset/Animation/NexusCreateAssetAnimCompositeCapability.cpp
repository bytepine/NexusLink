// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusCreateAssetAnimCompositeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Animation/AnimComposite.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FCreateAssetAnimCompositeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_anim_composite");
	Out.Description = TEXT("创建 AnimComposite（动画合成）资产；用 manage 添加片段。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("AnimComposite 包路径")))
		.Prop(TEXT("skeletonPath"), FNexusSchema::Str(TEXT("骨骼资产路径（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("composite"), TEXT("anim"), TEXT("sequence"), TEXT("combine") };
	Out.RelatedCapabilities = { TEXT("get_asset_anim_composite"), TEXT("manage_asset_anim_composite") };
	Out.WhenToUse = TEXT("创建空白 AnimComposite；需要 skeletonPath 时绑定骨骼");
}

FCapabilityResult FCreateAssetAnimCompositeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<UAnimComposite>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("AnimComposite already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UAnimComposite* Composite = NewObject<UAnimComposite>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Composite) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("AnimComposite 创建失败")); return; }

		if (Arguments->HasField(TEXT("skeletonPath")))
		{
			const FString SkelPath = Arguments->GetStringField(TEXT("skeletonPath"));
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkelPath);
			if (Skeleton) Composite->SetSkeleton(Skeleton);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Package, Composite, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),          Composite->GetName());
		Entry->SetStringField(TEXT("path"),          Composite->GetPathName());
		Entry->SetNumberField(TEXT("segmentCount"),  Composite->AnimationTrack.AnimSegments.Num());
		Entry->SetBoolField(TEXT("success"),         true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAnimCompositeCapability)
