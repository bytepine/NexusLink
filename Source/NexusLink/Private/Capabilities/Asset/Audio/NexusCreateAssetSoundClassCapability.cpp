// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_class");
	Out.Description = TEXT("Create SoundClass asset (volume/pitch hierarchy node).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundClass package path")))
		.Prop(TEXT("volume"),    FNexusSchema::Num(TEXT("Volume multiplier (default 1.0)")))
		.Prop(TEXT("pitch"),     FNexusSchema::Num(TEXT("Pitch multiplier (default 1.0)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("class"), TEXT("audio"), TEXT("volume"), TEXT("pitch") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_class"), TEXT("manage_asset_sound_class") };
	Out.WhenToUse = TEXT("Create SoundClass hierarchy node");
}

FCapabilityResult FCreateAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<USoundClass>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		USoundClass* SC = Cast<USoundClass>(Created.Asset);
		if (!SC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		if (Arguments->HasField(TEXT("volume"))) SC->Properties.Volume = (float)A.Num(TEXT("volume"));
		if (Arguments->HasField(TEXT("pitch")))  SC->Properties.Pitch  = (float)A.Num(TEXT("pitch"));

		FNexusAssetUtils::NotifyAndSaveCreated(SC->GetOutermost(), SC, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),    SC->GetName());
		Entry->SetStringField(TEXT("path"),    SC->GetPathName());
		Entry->SetNumberField(TEXT("volume"),  SC->Properties.Volume);
		Entry->SetNumberField(TEXT("pitch"),   SC->Properties.Pitch);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundClassCapability)
