// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PoseSearch/NexusGetAssetPoseSearchCapability.h"

#if WITH_POSE_SEARCH

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

void FNexusGetAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("GetAssetPoseSearch");
	Out.SearchAssetTypes = {TEXT("PoseSearchDatabase"), TEXT("PoseSearchSchema")};
	Out.Description = TEXT("Read PoseSearchDatabase or Schema overview. Use ManageAssetPoseSearch for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PoseSearchDatabase or Schema asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("database"), TEXT("Schema") };
	Out.RelatedCapabilities = { TEXT("ManageAssetPoseSearch"), TEXT("CreateAssetPoseSearch"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Read PoseSearch DB schema and anim asset count; use ManageAssetPoseSearch for writes");
}

FCapabilityResult FNexusGetAssetPoseSearchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), AssetPath);

		// 尝试加载 Database
		if (UPoseSearchDatabase* DB = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchDatabase>(AssetPath))
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("PoseSearchDatabase"));
			Entry->SetStringField(TEXT("name"), DB->GetName());

			if (DB->Schema)
				Entry->SetStringField(TEXT("Schema"), DB->Schema->GetPathName());

#if NX_UE_HAS_POSE_SEARCH_NUM_ANIMATION_ASSETS
			Entry->SetNumberField(TEXT("AnimationAssetCount"), DB->GetNumAnimationAssets());
#else
			Entry->SetNumberField(TEXT("AnimationAssetCount"), DB->GetAnimationAssets().Num());
#endif

			// Tags
			TArray<TSharedPtr<FJsonValue>> TagsArr;
			for (const FName& Tag : DB->Tags)
				TagsArr.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			Entry->SetArrayField(TEXT("Tags"), TagsArr);

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		// 尝试加载 Schema
		if (UPoseSearchSchema* Schema = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchSchema>(AssetPath))
		{
			Entry->SetStringField(TEXT("AssetType"), TEXT("PoseSearchSchema"));
			Entry->SetStringField(TEXT("name"), Schema->GetName());

			// Channels（5.4+ Channels 为 private，走 GetChannels）
			TArray<TSharedPtr<FJsonValue>> ChArr;
			for (const TObjectPtr<UPoseSearchFeatureChannel>& ChPtr : Schema->GetChannels())
			{
				const UPoseSearchFeatureChannel* Ch = ChPtr.Get();
				if (!Ch) continue;
				TSharedPtr<FJsonObject> ChObj = MakeShared<FJsonObject>();
				ChObj->SetStringField(TEXT("Class"), Ch->GetClass()->GetName());
				ChArr.Add(MakeShared<FJsonValueObject>(ChObj));
			}
			Entry->SetArrayField(TEXT("Channels"), ChArr);
			Entry->SetNumberField(TEXT("ChannelCount"), Schema->GetChannels().Num());

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("Error"), TEXT("PoseSearchDatabase or PoseSearchSchema not found"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
