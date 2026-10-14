// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetSoundAttenuation");
	Out.Description = TEXT("Create SoundAttenuation asset (attenuation curve/shape).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),       FNexusSchema::Str(TEXT("SoundAttenuation package path")))
		.Prop(TEXT("InnerRadius"),     FNexusSchema::Num(TEXT("Inner attenuation sphere radius (default 400)")))
		.Prop(TEXT("FalloffDistance"), FNexusSchema::Num(TEXT("Attenuation distance (default 3600)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("Sound"), TEXT("audio"), TEXT("distance"), TEXT("Radius") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundAttenuation"), TEXT("ManageAssetSoundAttenuation") };
	Out.WhenToUse = TEXT("Create sound attenuation settings asset");
}

FCapabilityResult FNexusCreateAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<USoundAttenuation>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		USoundAttenuation* SA = Cast<USoundAttenuation>(Created.Asset);
		if (!SA)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		if (Arguments->HasField(TEXT("InnerRadius")))
			SA->Attenuation.AttenuationShapeExtents.X = (float)A.Num(TEXT("InnerRadius"));
		if (Arguments->HasField(TEXT("FalloffDistance")))
			SA->Attenuation.FalloffDistance = (float)A.Num(TEXT("FalloffDistance"));

		FNexusAssetUtils::NotifyAndSaveCreated(SA->GetOutermost(), SA, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(SA);
		Entry->SetNumberField(TEXT("InnerRadius"),    SA->Attenuation.AttenuationShapeExtents.X);
		Entry->SetNumberField(TEXT("FalloffDistance"),SA->Attenuation.FalloffDistance);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetSoundAttenuationCapability)
