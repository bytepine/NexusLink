// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PoseSearch/NexusCreateAssetPoseSearchCapability.h"

#if WITH_POSE_SEARCH

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusMcpTool.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

void FCreateAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_pose_search");
	Out.Description = TEXT("Create PoseSearchDatabase or PoseSearchSchema (UE 5.4+).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("assetKind"), FNexusSchema::Enum(TEXT("Asset kind"), {
			TEXT("Database"), TEXT("Schema")
		}))
		.Prop(TEXT("schemaPath"), FNexusSchema::Str(TEXT("Optional Schema path when creating Database")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("database"), TEXT("schema") };
	Out.RelatedCapabilities = { TEXT("get_asset_pose_search"), TEXT("manage_asset_pose_search") };
	Out.WhenToUse = TEXT("Create PoseSearch Database/Schema from scratch; entry CRUD limited");
}

FCapabilityResult FCreateAssetPoseSearchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		FString Kind = TEXT("Database");
		Kind = FNexusArgs(Arguments).Str(TEXT("assetKind"), Kind);
		if (Kind.IsEmpty()) Kind = TEXT("Database");

		const bool bSchema = Kind.Equals(TEXT("Schema"), ESearchCase::IgnoreCase);
		if (!bSchema && !Kind.Equals(TEXT("Database"), ESearchCase::IgnoreCase))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				TEXT("assetKind must be Database or Schema"));
			return;
		}

		if (bSchema)
		{
			const FNexusAssetUtils::FAssetCreateOutcome Created =
				FNexusAssetUtils::CreatePlainAsset<UPoseSearchSchema>(AssetPath);
			if (!Created.Ok())
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
				return;
			}
			UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(Created.Asset);
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Schema->GetName());
			Entry->SetStringField(TEXT("path"), Schema->GetPathName());
			Entry->SetStringField(TEXT("assetKind"), TEXT("Schema"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UPoseSearchDatabase>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UPoseSearchDatabase* DB = Cast<UPoseSearchDatabase>(Created.Asset);

		FString SchemaPath;
		SchemaPath = FNexusArgs(Arguments).Str(TEXT("schemaPath"), SchemaPath);
		if (!SchemaPath.IsEmpty())
		{
			if (UPoseSearchSchema* Schema = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchSchema>(SchemaPath))
			{
				DB->Schema = Schema;
			}
			else
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("PoseSearchSchema not found: %s"), *SchemaPath));
				return;
			}
		}

		FNexusAssetUtils::NotifyAndSaveCreated(DB->GetOutermost(), DB, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), DB->GetName());
		Entry->SetStringField(TEXT("path"), DB->GetPathName());
		Entry->SetStringField(TEXT("assetKind"), TEXT("Database"));
		if (DB->Schema) Entry->SetStringField(TEXT("schemaPath"), DB->Schema->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
