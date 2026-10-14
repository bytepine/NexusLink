// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetSoundClass");
	Out.Description = TEXT("Create SoundClass asset (volume/pitch hierarchy node).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundClass package path")))
		.Prop(TEXT("Volume"),    FNexusSchema::Num(TEXT("Volume multiplier (default 1.0)")))
		.Prop(TEXT("Pitch"),     FNexusSchema::Num(TEXT("Pitch multiplier (default 1.0)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sound"), TEXT("Class"), TEXT("audio"), TEXT("Volume"), TEXT("Pitch") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundClass"), TEXT("ManageAssetSoundClass") };
	Out.WhenToUse = TEXT("Create SoundClass hierarchy node");
}

FCapabilityResult FNexusCreateAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));

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
		if (Arguments->HasField(TEXT("Volume"))) SC->Properties.Volume = (float)A.Num(TEXT("Volume"));
		if (Arguments->HasField(TEXT("Pitch")))  SC->Properties.Pitch  = (float)A.Num(TEXT("Pitch"));

		FNexusAssetUtils::NotifyAndSaveCreated(SC->GetOutermost(), SC, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(SC);
		Entry->SetNumberField(TEXT("Volume"),  SC->Properties.Volume);
		Entry->SetNumberField(TEXT("Pitch"),   SC->Properties.Pitch);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetSoundClassCapability)
