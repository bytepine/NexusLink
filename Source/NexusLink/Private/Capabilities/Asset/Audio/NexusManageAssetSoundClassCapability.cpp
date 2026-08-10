// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FManageAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_class");
	Out.SearchAssetTypes = {TEXT("SoundClass")};
	Out.Description = TEXT("设置 SoundClass 的 volume/pitch/lowPassFilter/attenuationScale。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("操作"), { TEXT("set") }))
		.Prop(TEXT("volume"),           FNexusSchema::Num(TEXT("音量倍数 [0,∞)")))
		.Prop(TEXT("pitch"),            FNexusSchema::Num(TEXT("音高倍数 [0,∞)")))
		.Prop(TEXT("lowPassFilter"),    FNexusSchema::Num(TEXT("低通滤波截频 (Hz)")))
		.Prop(TEXT("attenuationScale"), FNexusSchema::Num(TEXT("衰减距离缩放")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundClass 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
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

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("缺少 operations 或为空");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("无效的 operation 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = Op->HasField(TEXT("action")) ? Op->GetStringField(TEXT("action")).ToLower() : TEXT("");
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'（仅 set）"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Op->HasField(TEXT("volume")))           SC->Properties.Volume                   = (float)Op->GetNumberField(TEXT("volume"));
			if (Op->HasField(TEXT("pitch")))            SC->Properties.Pitch                    = (float)Op->GetNumberField(TEXT("pitch"));
			if (Op->HasField(TEXT("lowPassFilter")))    SC->Properties.LowPassFilterFrequency   = (float)Op->GetNumberField(TEXT("lowPassFilter"));
			if (Op->HasField(TEXT("attenuationScale"))) SC->Properties.AttenuationDistanceScale = (float)Op->GetNumberField(TEXT("attenuationScale"));

			SC->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),          SC->GetName());
			Entry->SetNumberField(TEXT("volume"),        SC->Properties.Volume);
			Entry->SetNumberField(TEXT("pitch"),         SC->Properties.Pitch);
			Entry->SetNumberField(TEXT("lowPassFilter"), SC->Properties.LowPassFilterFrequency);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundClassCapability)
