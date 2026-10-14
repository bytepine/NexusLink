// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSoundConcurrency");
	Out.SearchAssetTypes = {TEXT("SoundConcurrency")};
	Out.Description = TEXT("Read SoundConcurrency: MaxCount/ResolutionRule/RetriggerTime.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundConcurrency asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("maxcount"), TEXT("audio"), TEXT("Limit") };
	Out.RelatedCapabilities = { TEXT("CreateAssetSoundConcurrency"), TEXT("ManageAssetSoundConcurrency") };
}

FCapabilityResult FNexusGetAssetSoundConcurrencyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		USoundConcurrency* SC = LoadObject<USoundConcurrency>(nullptr, *AssetPath);
		if (!SC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load SoundConcurrency: %s"), *AssetPath));
			return;
		}

		const UEnum* RuleEnum = StaticEnum<EMaxConcurrentResolutionRule::Type>();
		const FString RuleStr = RuleEnum
			? RuleEnum->GetNameStringByValue((int64)SC->Concurrency.ResolutionRule)
			: FString::FromInt((int32)SC->Concurrency.ResolutionRule);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),            SC->GetName());
		Entry->SetStringField(TEXT("Path"),            SC->GetPathName());
		Entry->SetNumberField(TEXT("MaxCount"),        SC->Concurrency.MaxCount);
		Entry->SetStringField(TEXT("ResolutionRule"),  RuleStr);
		Entry->SetNumberField(TEXT("ResolutionRuleValue"), (double)(int32)SC->Concurrency.ResolutionRule);
		Entry->SetBoolField(TEXT("bLimitToOwner"),      SC->Concurrency.bLimitToOwner != 0);
		Entry->SetNumberField(TEXT("RetriggerTime"),   SC->Concurrency.RetriggerTime);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSoundConcurrencyCapability)
