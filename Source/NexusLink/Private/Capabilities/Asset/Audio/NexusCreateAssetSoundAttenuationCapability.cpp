// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_attenuation");
	Out.Description = TEXT("Create SoundAttenuation asset (attenuation curve/shape).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),       FNexusSchema::Str(TEXT("SoundAttenuation package path")))
		.Prop(TEXT("innerRadius"),     FNexusSchema::Num(TEXT("Inner attenuation sphere radius (default 400)")))
		.Prop(TEXT("falloffDistance"), FNexusSchema::Num(TEXT("Attenuation distance (default 3600)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("sound"), TEXT("audio"), TEXT("distance"), TEXT("radius") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_attenuation"), TEXT("manage_asset_sound_attenuation") };
	Out.WhenToUse = TEXT("Create sound attenuation settings asset");
}

FCapabilityResult FCreateAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

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
		if (Arguments->HasField(TEXT("innerRadius")))
			SA->Attenuation.AttenuationShapeExtents.X = (float)A.Num(TEXT("innerRadius"));
		if (Arguments->HasField(TEXT("falloffDistance")))
			SA->Attenuation.FalloffDistance = (float)A.Num(TEXT("falloffDistance"));

		FNexusAssetUtils::NotifyAndSaveCreated(SA->GetOutermost(), SA, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),           SA->GetName());
		Entry->SetStringField(TEXT("path"),           SA->GetPathName());
		Entry->SetNumberField(TEXT("innerRadius"),    SA->Attenuation.AttenuationShapeExtents.X);
		Entry->SetNumberField(TEXT("falloffDistance"),SA->Attenuation.FalloffDistance);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundAttenuationCapability)
