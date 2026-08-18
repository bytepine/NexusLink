// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FManageAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_concurrency");
	Out.SearchAssetTypes = {TEXT("SoundConcurrency")};
	Out.Description = TEXT("Set SoundConcurrency: maxCount/resolutionRuleValue/retriggerTime/limitToOwner.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),              FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("maxCount"),            FNexusSchema::Int(TEXT("Max concurrent instances (≥1)")))
		.Prop(TEXT("resolutionRuleValue"), FNexusSchema::Int(TEXT("EMaxConcurrentResolutionRule int: 0=PreventNew,1=StopOldest…")))
		.Prop(TEXT("retriggerTime"),       FNexusSchema::Num(TEXT("Retrigger time (sec); min interval before same sound retriggers")))
		.Prop(TEXT("limitToOwner"),        FNexusSchema::Bool(TEXT("Limit concurrency per Owner")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundConcurrency asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
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
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		USoundConcurrency* SC = LoadObject<USoundConcurrency>(nullptr, *AssetPath);
		if (!SC)
		{
			OutError = FString::Printf(TEXT("Failed to load SoundConcurrency: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = FNexusArgs(Op).Str(TEXT("action")).ToLower();
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s' (set only)"), *Action));
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
