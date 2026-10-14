// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetSoundSubmix");
	Out.Description = TEXT("Create SoundSubmix asset (mix bus node).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundSubmix package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("Sound"), TEXT("audio"), TEXT("mix") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundSubmix"), TEXT("ManageAssetSoundSubmix") };
	Out.WhenToUse = TEXT("Create SoundSubmix from scratch; volume via ManageAssetSoundSubmix");
}

FCapabilityResult FNexusCreateAssetSoundSubmixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<USoundSubmix>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		USoundSubmix* SM = Cast<USoundSubmix>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, SM);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetSoundSubmixCapability)
