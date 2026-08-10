// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FManageAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_concurrency");
	Out.SearchAssetTypes = {TEXT("SoundConcurrency")};
	Out.Description = TEXT("设置 SoundConcurrency：maxCount/resolutionRuleValue/retriggerTime/limitToOwner。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),              FNexusSchema::Enum(TEXT("操作"), { TEXT("set") }))
		.Prop(TEXT("maxCount"),            FNexusSchema::Int(TEXT("最大并发实例数（≥1）")))
		.Prop(TEXT("resolutionRuleValue"), FNexusSchema::Int(TEXT("EMaxConcurrentResolutionRule int 值：0=PreventNew,1=StopOldest…")))
		.Prop(TEXT("retriggerTime"),       FNexusSchema::Num(TEXT("重触发时间（秒），同一声音再次触发的最小间隔")))
		.Prop(TEXT("limitToOwner"),        FNexusSchema::Bool(TEXT("是否按 Owner 限制并发")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundConcurrency 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
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

			if (Op->HasField(TEXT("maxCount")))
				SC->Concurrency.MaxCount = FMath::Max(1, (int32)Op->GetNumberField(TEXT("maxCount")));
			if (Op->HasField(TEXT("resolutionRuleValue")))
				SC->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::Type((int32)Op->GetNumberField(TEXT("resolutionRuleValue")));
			if (Op->HasField(TEXT("retriggerTime")))
				SC->Concurrency.RetriggerTime = (float)Op->GetNumberField(TEXT("retriggerTime"));
			if (Op->HasField(TEXT("limitToOwner")))
				SC->Concurrency.bLimitToOwner = Op->GetBoolField(TEXT("limitToOwner")) ? 1 : 0;

			SC->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),     SC->GetName());
			Entry->SetNumberField(TEXT("maxCount"), SC->Concurrency.MaxCount);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundConcurrencyCapability)
