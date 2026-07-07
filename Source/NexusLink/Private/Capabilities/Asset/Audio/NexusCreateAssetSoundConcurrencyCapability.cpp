// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_concurrency");
	Out.Description = TEXT("创建 SoundConcurrency 资产（最大并发实例数限制）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundConcurrency 包路径")))
		.Prop(TEXT("maxCount"),  FNexusSchema::Int(TEXT("最大并发实例数（默认16）"), 16, 1))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("sound"), TEXT("limit"), TEXT("audio") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_concurrency"), TEXT("manage_asset_sound_concurrency") };
	Out.WhenToUse = TEXT("创建声音并发限制资产");
}

FCapabilityResult FCreateAssetSoundConcurrencyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<USoundConcurrency>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("SoundConcurrency already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		USoundConcurrency* SC = NewObject<USoundConcurrency>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!SC) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("SoundConcurrency 创建失败")); return; }

		if (Arguments->HasField(TEXT("maxCount")))
			SC->Concurrency.MaxCount = FMath::Max(1, (int32)Arguments->GetNumberField(TEXT("maxCount")));

		FNexusAssetUtils::NotifyAndSaveCreated(Package, SC, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     SC->GetName());
		Entry->SetStringField(TEXT("path"),     SC->GetPathName());
		Entry->SetNumberField(TEXT("maxCount"), SC->Concurrency.MaxCount);
		Entry->SetBoolField(TEXT("success"),    true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundConcurrencyCapability)
