// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusCreateAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FCreateAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_media_source");
	Out.Description = TEXT("Create FileMediaSource. Optional mediaPath. Playback via interact_runtime_actor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("mediaPath"), FNexusSchema::Str(TEXT("Media file path (optional)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("movie"), TEXT("mp4"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("get_asset_media_source"), TEXT("manage_asset_media_source") };
	Out.WhenToUse = TEXT("Create FileMediaSource; does not handle playback");
}

FCapabilityResult FCreateAssetMediaSourceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		if (LoadObject<UFileMediaSource>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("FileMediaSource already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UFileMediaSource* Source = NewObject<UFileMediaSource>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Source) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Creation failed")); return; }
		FString FilePath;
		if (Arguments->TryGetStringField(TEXT("mediaPath"), FilePath) && !FilePath.IsEmpty())
		{
			Source->SetFilePath(FilePath);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Source, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Source->GetName());
		Entry->SetStringField(TEXT("path"), Source->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMediaSourceCapability)
