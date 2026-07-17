// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_class");
	Out.Description = TEXT("创建 SoundClass 资产（音量/音高层级管理节点）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundClass 包路径")))
		.Prop(TEXT("volume"),    FNexusSchema::Num(TEXT("音量倍数（默认1.0）")))
		.Prop(TEXT("pitch"),     FNexusSchema::Num(TEXT("音高倍数（默认1.0）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("class"), TEXT("audio"), TEXT("volume"), TEXT("pitch") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_class"), TEXT("manage_asset_sound_class") };
	Out.WhenToUse = TEXT("创建 SoundClass 层级节点");
}

FCapabilityResult FCreateAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<USoundClass>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("SoundClass already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		USoundClass* SC = NewObject<USoundClass>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!SC) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("SoundClass 创建失败")); return; }

		if (Arguments->HasField(TEXT("volume"))) SC->Properties.Volume = (float)Arguments->GetNumberField(TEXT("volume"));
		if (Arguments->HasField(TEXT("pitch")))  SC->Properties.Pitch  = (float)Arguments->GetNumberField(TEXT("pitch"));

		FNexusAssetUtils::NotifyAndSaveCreated(Package, SC, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),    SC->GetName());
		Entry->SetStringField(TEXT("path"),    SC->GetPathName());
		Entry->SetNumberField(TEXT("volume"),  SC->Properties.Volume);
		Entry->SetNumberField(TEXT("pitch"),   SC->Properties.Pitch);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundClassCapability)
