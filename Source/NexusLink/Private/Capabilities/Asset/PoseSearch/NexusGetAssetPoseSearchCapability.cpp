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

void FGetAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("get_asset_pose_search");
	Out.SearchAssetTypes = {TEXT("PoseSearchDatabase"), TEXT("PoseSearchSchema")};
	Out.Description = TEXT("Read PoseSearchDatabase or Schema overview. Use manage_asset_pose_search for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PoseSearchDatabase or Schema asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("database"), TEXT("schema") };
	Out.RelatedCapabilities = { TEXT("manage_asset_pose_search"), TEXT("create_asset_pose_search"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Read PoseSearch DB schema and anim asset count; use manage_asset_pose_search for writes");
}

FCapabilityResult FGetAssetPoseSearchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);

		// 尝试加载 Database
		if (UPoseSearchDatabase* DB = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchDatabase>(AssetPath))
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("PoseSearchDatabase"));
			Entry->SetStringField(TEXT("name"), DB->GetName());

			if (DB->Schema)
				Entry->SetStringField(TEXT("schema"), DB->Schema->GetPathName());

			Entry->SetNumberField(TEXT("animationAssetCount"), DB->GetNumAnimationAssets());

			// Tags
			TArray<TSharedPtr<FJsonValue>> TagsArr;
			for (const FName& Tag : DB->Tags)
				TagsArr.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			Entry->SetArrayField(TEXT("tags"), TagsArr);

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		// 尝试加载 Schema
		if (UPoseSearchSchema* Schema = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchSchema>(AssetPath))
		{
			Entry->SetStringField(TEXT("assetType"), TEXT("PoseSearchSchema"));
			Entry->SetStringField(TEXT("name"), Schema->GetName());

			// Channels
			TArray<TSharedPtr<FJsonValue>> ChArr;
			for (const UPoseSearchFeatureChannel* Ch : Schema->Channels)
			{
				if (!Ch) continue;
				TSharedPtr<FJsonObject> ChObj = MakeShared<FJsonObject>();
				ChObj->SetStringField(TEXT("class"), Ch->GetClass()->GetName());
				ChArr.Add(MakeShared<FJsonValueObject>(ChObj));
			}
			Entry->SetArrayField(TEXT("channels"), ChArr);
			Entry->SetNumberField(TEXT("channelCount"), Schema->Channels.Num());

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("error"), TEXT("PoseSearchDatabase or PoseSearchSchema not found"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
