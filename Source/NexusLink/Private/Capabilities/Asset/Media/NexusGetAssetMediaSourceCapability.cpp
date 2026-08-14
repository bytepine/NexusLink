// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusGetAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FGetAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_media_source");
	Out.SearchAssetTypes = {TEXT("FileMediaSource"), TEXT("MediaSource")};
	Out.Description = TEXT("读取 FileMediaSource：mediaPath。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MediaSource 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("movie"), TEXT("mp4"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("manage_asset_media_source"), TEXT("create_asset_media_source") };
}

FCapabilityResult FGetAssetMediaSourceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UFileMediaSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UFileMediaSource>(AssetPath);
		if (!Source)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 FileMediaSource 失败: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Source->GetName());
		Entry->SetStringField(TEXT("path"), Source->GetPathName());
		Entry->SetStringField(TEXT("mediaPath"), Source->GetFilePath());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetMediaSourceCapability)
