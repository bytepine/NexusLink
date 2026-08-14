// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusCreateAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FCreateAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_media_source");
	Out.Description = TEXT("创建 FileMediaSource。可选 filePath。播放走 interact_runtime_actor。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("filePath"), FNexusSchema::Str(TEXT("媒体文件路径（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("movie"), TEXT("mp4"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("get_asset_media_source"), TEXT("manage_asset_media_source") };
	Out.WhenToUse = TEXT("新建 FileMediaSource；不负责播放");
}

FCapabilityResult FCreateAssetMediaSourceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UFileMediaSource>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("FileMediaSource already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UFileMediaSource* Source = NewObject<UFileMediaSource>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Source) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FString FilePath;
		if (Arguments->TryGetStringField(TEXT("filePath"), FilePath) && !FilePath.IsEmpty())
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
