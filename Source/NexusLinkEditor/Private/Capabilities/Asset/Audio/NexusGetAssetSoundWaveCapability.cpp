// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundWaveCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSoundWaveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSoundWave");
	Out.SearchAssetTypes = {TEXT("SoundWave")};
	Out.Description = TEXT("Inspect SoundWave snapshot. Writes via ManageAssetSoundWave.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundWave asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("Sound"), TEXT("wave"), TEXT("sfx"), TEXT("Duration") };
	Out.RelatedCapabilities = { TEXT("ManageAssetSoundWave"), TEXT("SearchAsset"), TEXT("GetAssetSoundCue"), TEXT("GetAssetRefs") };
	Out.WhenToUse = TEXT("Read wave metadata; use ManageAssetSoundWave for writes");
}

FCapabilityResult FNexusGetAssetSoundWaveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		USoundWave* Wave = FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(Path);
		if (!Wave)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("SoundWave not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Wave->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("SoundWave"));
		Entry->SetNumberField(TEXT("Duration"), Wave->GetDuration());
		Entry->SetNumberField(TEXT("SampleRate"), Wave->GetSampleRateForCurrentPlatform());
		Entry->SetNumberField(TEXT("NumChannels"), Wave->NumChannels);
		Entry->SetNumberField(TEXT("TotalSamples"), static_cast<double>(Wave->TotalSamples));
		Entry->SetBoolField(TEXT("bStreaming"), Wave->bStreaming);
		Entry->SetBoolField(TEXT("bLooping"), Wave->bLooping);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSoundWaveCapability)
