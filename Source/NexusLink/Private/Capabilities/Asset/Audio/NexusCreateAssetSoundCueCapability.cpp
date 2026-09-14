// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundCueCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundCue.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundCueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_cue");
	Out.Description = TEXT("Create empty SoundCue. Add nodes via manage_asset_sound_cue.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundCue package path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("cue"), TEXT("audio"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_cue"), TEXT("manage_asset_sound_cue") };
	Out.WhenToUse = TEXT("Create SoundCue; aligns with create_asset_sound_class");
}

FCapabilityResult FCreateAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
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

REGISTER_MCP_CAPABILITY(FCreateAssetSoundCueCapability)
