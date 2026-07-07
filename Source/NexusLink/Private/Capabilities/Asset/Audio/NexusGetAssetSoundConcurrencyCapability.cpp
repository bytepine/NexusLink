// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FGetAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_sound_concurrency");
	Out.Description = TEXT("读取 SoundConcurrency：maxCount/resolutionRule/retriggerTime。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundConcurrency 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("maxcount"), TEXT("audio"), TEXT("limit") };
	Out.RelatedCapabilities = { TEXT("create_asset_sound_concurrency"), TEXT("manage_asset_sound_concurrency") };
}

FCapabilityResult FGetAssetSoundConcurrencyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		USoundConcurrency* SC = LoadObject<USoundConcurrency>(nullptr, *AssetPath);
		if (!SC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载 SoundConcurrency 失败: %s"), *AssetPath));
			return;
		}

		const UEnum* RuleEnum = StaticEnum<EMaxConcurrentResolutionRule::Type>();
		const FString RuleStr = RuleEnum
			? RuleEnum->GetNameStringByValue((int64)SC->Concurrency.ResolutionRule)
			: FString::FromInt((int32)SC->Concurrency.ResolutionRule);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),            SC->GetName());
		Entry->SetStringField(TEXT("path"),            SC->GetPathName());
		Entry->SetNumberField(TEXT("maxCount"),        SC->Concurrency.MaxCount);
		Entry->SetStringField(TEXT("resolutionRule"),  RuleStr);
		Entry->SetNumberField(TEXT("resolutionRuleValue"), (double)(int32)SC->Concurrency.ResolutionRule);
		Entry->SetBoolField(TEXT("limitToOwner"),      SC->Concurrency.bLimitToOwner != 0);
		Entry->SetNumberField(TEXT("retriggerTime"),   SC->Concurrency.RetriggerTime);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundConcurrencyCapability)
