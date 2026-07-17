// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FManageAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_concurrency");
	Out.SearchAssetTypes = {TEXT("SoundConcurrency")};
	Out.Description = TEXT("设置 SoundConcurrency：maxCount/resolutionRuleValue/retriggerTime/limitToOwner。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),          FNexusSchema::Str(TEXT("SoundConcurrency 资产路径")))
		.Prop(TEXT("maxCount"),               FNexusSchema::Int(TEXT("最大并发实例数（≥1）")))
		.Prop(TEXT("resolutionRuleValue"),    FNexusSchema::Int(TEXT("EMaxConcurrentResolutionRule int 值：0=PreventNew,1=StopOldest…")))
		.Prop(TEXT("retriggerTime"),          FNexusSchema::Num(TEXT("重触发阈值（秒），同一声音再次触发的最小间隔")))
		.Prop(TEXT("limitToOwner"),           FNexusSchema::Bool(TEXT("是否按 Owner 限制并发")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("maxcount"), TEXT("audio"), TEXT("limit") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_concurrency"), TEXT("create_asset_sound_concurrency") };
}

FCapabilityResult FManageAssetSoundConcurrencyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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
			OutError = FString::Printf(TEXT("加载 SoundConcurrency 失败: %s"), *AssetPath);
			return;
		}

		if (Arguments->HasField(TEXT("maxCount")))
			SC->Concurrency.MaxCount = FMath::Max(1, (int32)Arguments->GetNumberField(TEXT("maxCount")));
		if (Arguments->HasField(TEXT("resolutionRuleValue")))
			SC->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::Type((int32)Arguments->GetNumberField(TEXT("resolutionRuleValue")));
		if (Arguments->HasField(TEXT("retriggerTime")))
			SC->Concurrency.RetriggerTime = (float)Arguments->GetNumberField(TEXT("retriggerTime"));
		if (Arguments->HasField(TEXT("limitToOwner")))
			SC->Concurrency.bLimitToOwner = Arguments->GetBoolField(TEXT("limitToOwner")) ? 1 : 0;

		SC->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     SC->GetName());
		Entry->SetNumberField(TEXT("maxCount"), SC->Concurrency.MaxCount);
		Entry->SetBoolField(TEXT("success"),    true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundConcurrencyCapability)
