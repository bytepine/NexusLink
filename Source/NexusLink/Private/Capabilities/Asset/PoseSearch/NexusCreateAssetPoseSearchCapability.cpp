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
		Arguments->TryGetStringField(TEXT("assetKind"), Kind);
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
			if (LoadObject<UPoseSearchSchema>(nullptr, *AssetPath))
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("PoseSearchSchema already exists: %s"), *AssetPath));
				return;
			}
			UPackage* Package = CreatePackage(*AssetPath);
			if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
			const FString AssetName = FPaths::GetBaseFilename(AssetPath);
			UPoseSearchSchema* Schema = NewObject<UPoseSearchSchema>(Package, *AssetName, RF_Public | RF_Standalone);
			if (!Schema) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("PoseSearchSchema Createfailed")); return; }
			FNexusAssetUtils::NotifyAndSaveCreated(Package, Schema, AssetPath);
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Schema->GetName());
			Entry->SetStringField(TEXT("path"), Schema->GetPathName());
			Entry->SetStringField(TEXT("assetKind"), TEXT("Schema"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		if (LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("PoseSearchDatabase already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPoseSearchDatabase* DB = NewObject<UPoseSearchDatabase>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!DB) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("PoseSearchDatabase Createfailed")); return; }

		FString SchemaPath;
		Arguments->TryGetStringField(TEXT("schemaPath"), SchemaPath);
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

		FNexusAssetUtils::NotifyAndSaveCreated(Package, DB, AssetPath);
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
