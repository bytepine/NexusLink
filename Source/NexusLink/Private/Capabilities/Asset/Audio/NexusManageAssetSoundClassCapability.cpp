// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FManageAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_class");
	Out.Description = TEXT("设置 SoundClass 的 volume/pitch/lowPassFilter/attenuationScale。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),       FNexusSchema::Str(TEXT("SoundClass 资产路径")))
		.Prop(TEXT("volume"),              FNexusSchema::Num(TEXT("音量倍数 [0,∞)")))
		.Prop(TEXT("pitch"),               FNexusSchema::Num(TEXT("音高倍数 [0,∞)")))
		.Prop(TEXT("lowPassFilter"),       FNexusSchema::Num(TEXT("低通滤波截频 (Hz)")))
		.Prop(TEXT("attenuationScale"),    FNexusSchema::Num(TEXT("衰减距离缩放")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("class"), TEXT("volume"), TEXT("pitch"), TEXT("filter") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_class"), TEXT("create_asset_sound_class") };
}

FCapabilityResult FManageAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		USoundClass* SC = LoadObject<USoundClass>(nullptr, *AssetPath);
		if (!SC)
		{
			OutError = FString::Printf(TEXT("加载 SoundClass 失败: %s"), *AssetPath);
			return;
		}

		if (Arguments->HasField(TEXT("volume")))           SC->Properties.Volume                    = (float)Arguments->GetNumberField(TEXT("volume"));
		if (Arguments->HasField(TEXT("pitch")))            SC->Properties.Pitch                     = (float)Arguments->GetNumberField(TEXT("pitch"));
		if (Arguments->HasField(TEXT("lowPassFilter")))    SC->Properties.LowPassFilterFrequency    = (float)Arguments->GetNumberField(TEXT("lowPassFilter"));
		if (Arguments->HasField(TEXT("attenuationScale"))) SC->Properties.AttenuationDistanceScale  = (float)Arguments->GetNumberField(TEXT("attenuationScale"));

		SC->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             SC->GetName());
		Entry->SetNumberField(TEXT("volume"),           SC->Properties.Volume);
		Entry->SetNumberField(TEXT("pitch"),            SC->Properties.Pitch);
		Entry->SetNumberField(TEXT("lowPassFilter"),    SC->Properties.LowPassFilterFrequency);
		Entry->SetBoolField(TEXT("success"),            true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundClassCapability)
