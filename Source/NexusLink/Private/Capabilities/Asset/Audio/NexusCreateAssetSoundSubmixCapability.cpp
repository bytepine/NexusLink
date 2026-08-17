// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_submix");
	Out.Description = TEXT("创建 SoundSubmix 资产（混音节点）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundSubmix 包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("sound"), TEXT("audio"), TEXT("mix") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_submix"), TEXT("manage_asset_sound_submix") };
	Out.WhenToUse = TEXT("从零创建 SoundSubmix；音量用 manage_asset_sound_submix");
}

FCapabilityResult FCreateAssetSoundSubmixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<USoundSubmix>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("SoundSubmix already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		USoundSubmix* SM = NewObject<USoundSubmix>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!SM) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("SoundSubmix 创建失败")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, SM, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), SM->GetName());
		Entry->SetStringField(TEXT("path"), SM->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundSubmixCapability)
