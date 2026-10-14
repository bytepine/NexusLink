// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetSoundConcurrency");
	Out.Description = TEXT("Create SoundConcurrency asset (max concurrent instances).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundConcurrency package path")))
		.Prop(TEXT("MaxCount"),  FNexusSchema::Int(TEXT("Max concurrent instances (default 16)"), 16, 1))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("Sound"), TEXT("Limit"), TEXT("audio") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundConcurrency"), TEXT("ManageAssetSoundConcurrency") };
	Out.WhenToUse = TEXT("Create sound concurrency limit asset");
}

FCapabilityResult FNexusCreateAssetSoundConcurrencyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<USoundConcurrency>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		USoundConcurrency* SC = Cast<USoundConcurrency>(Created.Asset);
		if (!SC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		if (Arguments->HasField(TEXT("MaxCount")))
			SC->Concurrency.MaxCount = FMath::Max(1, (int32)A.Num(TEXT("MaxCount")));

		FNexusAssetUtils::NotifyAndSaveCreated(SC->GetOutermost(), SC, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(SC);
		Entry->SetNumberField(TEXT("MaxCount"), SC->Concurrency.MaxCount);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetSoundConcurrencyCapability)
