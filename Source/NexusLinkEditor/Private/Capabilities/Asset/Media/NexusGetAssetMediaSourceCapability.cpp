// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusGetAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FNexusGetAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetMediaSource");
	Out.SearchAssetTypes = {TEXT("FileMediaSource"), TEXT("MediaSource")};
	Out.Description = TEXT("Read FileMediaSource: MediaPath.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MediaSource asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("movie"), TEXT("mp4"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("ManageAssetMediaSource"), TEXT("CreateAssetMediaSource") };
}

FCapabilityResult FNexusGetAssetMediaSourceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UFileMediaSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UFileMediaSource>(AssetPath);
		if (!Source)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load FileMediaSource: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Source->GetName());
		Entry->SetStringField(TEXT("Path"), Source->GetPathName());
		Entry->SetStringField(TEXT("MediaPath"), Source->GetFilePath());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetMediaSourceCapability)
