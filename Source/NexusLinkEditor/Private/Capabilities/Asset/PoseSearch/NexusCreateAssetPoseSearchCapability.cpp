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

void FNexusCreateAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetPoseSearch");
	Out.Description = TEXT("Create PoseSearchDatabase or PoseSearchSchema (UE 5.4+).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("AssetKind"), FNexusSchema::Enum(TEXT("Asset kind"), {
			TEXT("Database"), TEXT("Schema")
		}))
		.Prop(TEXT("SchemaPath"), FNexusSchema::Str(TEXT("Optional Schema path when creating Database")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("database"), TEXT("Schema") };
	Out.RelatedCapabilities = { TEXT("GetAssetPoseSearch"), TEXT("ManageAssetPoseSearch") };
	Out.WhenToUse = TEXT("Create PoseSearch Database/Schema from scratch; entry CRUD limited");
}

FCapabilityResult FNexusCreateAssetPoseSearchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		FString Kind = TEXT("Database");
		Kind = FNexusArgs(Arguments).Str(TEXT("AssetKind"), Kind);
		if (Kind.IsEmpty()) Kind = TEXT("Database");

		const bool bSchema = Kind.Equals(TEXT("Schema"), ESearchCase::IgnoreCase);
		if (!bSchema && !Kind.Equals(TEXT("Database"), ESearchCase::IgnoreCase))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				TEXT("AssetKind must be Database or Schema"));
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
			TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(Schema);
			Entry->SetStringField(TEXT("AssetKind"), TEXT("Schema"));
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
		SchemaPath = FNexusArgs(Arguments).Str(TEXT("SchemaPath"), SchemaPath);
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
		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(DB);
		Entry->SetStringField(TEXT("AssetKind"), TEXT("Database"));
		if (DB->Schema) Entry->SetStringField(TEXT("SchemaPath"), DB->Schema->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
