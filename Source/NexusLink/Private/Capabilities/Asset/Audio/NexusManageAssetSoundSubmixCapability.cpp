// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FManageAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_submix");
	Out.SearchAssetTypes = {TEXT("SoundSubmix")};
	Out.Description = TEXT("设置 SoundSubmix 音量（UE5.1+ 用 dB 字段，见 InputSchema）。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),    FNexusSchema::Str(TEXT("SoundSubmix 资产路径")))
		.Prop(TEXT("outputVolume"),     FNexusSchema::Num(TEXT("输出音量线性 [0,1]（UE4/5.0）")))
		.Prop(TEXT("wetLevel"),         FNexusSchema::Num(TEXT("湿信号线性 [0,1]（UE4/5.0）")))
		.Prop(TEXT("dryLevel"),         FNexusSchema::Num(TEXT("干信号线性 [0,1]（UE4/5.0）")))
		.Prop(TEXT("outputVolumeDB"),   FNexusSchema::Num(TEXT("输出音量 dB [-96,0]（UE5.1+）")))
		.Prop(TEXT("wetLevelDB"),       FNexusSchema::Num(TEXT("湿信号 dB [-96,0]（UE5.1+）")))
		.Prop(TEXT("dryLevelDB"),       FNexusSchema::Num(TEXT("干信号 dB [-96,0]（UE5.1+）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("sound"), TEXT("audio"), TEXT("volume"), TEXT("wet"), TEXT("dry") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_submix") };
}

FCapabilityResult FManageAssetSoundSubmixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		USoundSubmix* SM = LoadObject<USoundSubmix>(nullptr, *AssetPath);
		if (!SM)
		{
			OutError = FString::Printf(TEXT("加载 SoundSubmix 失败: %s"), *AssetPath);
			return;
		}

#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
		if (Arguments->HasField(TEXT("outputVolume"))) SM->OutputVolume = FMath::Clamp((float)Arguments->GetNumberField(TEXT("outputVolume")), 0.f, 1.f);
		if (Arguments->HasField(TEXT("wetLevel")))     SM->WetLevel     = FMath::Clamp((float)Arguments->GetNumberField(TEXT("wetLevel")),     0.f, 1.f);
		if (Arguments->HasField(TEXT("dryLevel")))     SM->DryLevel     = FMath::Clamp((float)Arguments->GetNumberField(TEXT("dryLevel")),     0.f, 1.f);
#else
		if (Arguments->HasField(TEXT("outputVolumeDB"))) SM->OutputVolumeModulation.Value = FMath::Clamp((float)Arguments->GetNumberField(TEXT("outputVolumeDB")), -96.f, 0.f);
		if (Arguments->HasField(TEXT("wetLevelDB")))     SM->WetLevelModulation.Value     = FMath::Clamp((float)Arguments->GetNumberField(TEXT("wetLevelDB")),     -96.f, 0.f);
		if (Arguments->HasField(TEXT("dryLevelDB")))     SM->DryLevelModulation.Value     = FMath::Clamp((float)Arguments->GetNumberField(TEXT("dryLevelDB")),     -96.f, 0.f);
#endif

		SM->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),    SM->GetName());
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
		Entry->SetNumberField(TEXT("outputVolume"),  SM->OutputVolume);
		Entry->SetNumberField(TEXT("wetLevel"),      SM->WetLevel);
		Entry->SetNumberField(TEXT("dryLevel"),      SM->DryLevel);
#else
		Entry->SetNumberField(TEXT("outputVolumeDB"), SM->OutputVolumeModulation.Value);
		Entry->SetNumberField(TEXT("wetLevelDB"),     SM->WetLevelModulation.Value);
		Entry->SetNumberField(TEXT("dryLevelDB"),     SM->DryLevelModulation.Value);
#endif
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundSubmixCapability)
