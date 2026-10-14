// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusCreateAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetMediaSource");
	Out.Description = TEXT("Create FileMediaSource. Optional MediaPath. Playback via interact_runtime_actor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("MediaPath"), FNexusSchema::Str(TEXT("Media file path (optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("movie"), TEXT("mp4"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("GetAssetMediaSource"), TEXT("ManageAssetMediaSource") };
	Out.WhenToUse = TEXT("Create FileMediaSource; does not handle playback");
}

FCapabilityResult FNexusCreateAssetMediaSourceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UFileMediaSource>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UFileMediaSource* Source = Cast<UFileMediaSource>(Created.Asset);
		if (!Source)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		FString FilePath;
		if (Arguments->TryGetStringField(TEXT("MediaPath"), FilePath) && !FilePath.IsEmpty())
		{
			Source->SetFilePath(FilePath);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Source->GetOutermost(), Source, AssetPath);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Source);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetMediaSourceCapability)
