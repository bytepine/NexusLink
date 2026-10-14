// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundCueCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundCue.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetSoundCueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetSoundCue");
	Out.Description = TEXT("Create empty SoundCue. Add nodes via ManageAssetSoundCue.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundCue package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sound"), TEXT("cue"), TEXT("audio"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundCue"), TEXT("ManageAssetSoundCue") };
	Out.WhenToUse = TEXT("Create SoundCue; aligns with CreateAssetSoundClass");
}

FCapabilityResult FNexusCreateAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<USoundCue>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		USoundCue* Cue = Cast<USoundCue>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Cue);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetSoundCueCapability)
