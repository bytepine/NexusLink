// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundWaveCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FManageAssetSoundWaveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_wave");
	Out.Description = TEXT("编辑 SoundWave 属性。action=set_property。音量/循环等。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("SoundWave 资产路径")))
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("操作"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("属性路径（如 Volume/Looping）")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("属性新值字符串")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("wave"), TEXT("volume"), TEXT("loop"), TEXT("sound") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_wave"), TEXT("get_asset_sound_cue") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 SoundWave 音量/循环/衰减；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetSoundWaveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath, Action;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("action"), Action, OutEntries, {{TEXT("assetPath"), AssetPath}})) return;

		USoundWave* Wave = FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(AssetPath);
		if (!Wave)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("SoundWave 未找到: %s"), *AssetPath));
			return;
		}

		FString PropPath, Value;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("propertyPath"), PropPath);
			Arguments->TryGetStringField(TEXT("value"), Value);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
		{
			if (PropPath.IsEmpty() || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(Wave, { PropPath }, 0, Value, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Wave->MarkPackageDirty();
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundWaveCapability)
