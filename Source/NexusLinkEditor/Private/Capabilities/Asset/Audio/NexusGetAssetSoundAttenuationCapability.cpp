// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSoundAttenuation");
	Out.SearchAssetTypes = {TEXT("SoundAttenuation")};
	Out.Description = TEXT("Read SoundAttenuation: shape/InnerRadius/FalloffDistance/bAttenuate/bSpatialize.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundAttenuation asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("Sound"), TEXT("distance"), TEXT("Radius"), TEXT("Shape") };
	Out.RelatedCapabilities = { TEXT("CreateAssetSoundAttenuation"), TEXT("ManageAssetSoundAttenuation") };
}

FCapabilityResult FNexusGetAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		USoundAttenuation* SA = LoadObject<USoundAttenuation>(nullptr, *AssetPath);
		if (!SA)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load SoundAttenuation: %s"), *AssetPath));
			return;
		}

		const UEnum* ShapeEnum = StaticEnum<EAttenuationShape::Type>();
		const FString ShapeStr = ShapeEnum
			? ShapeEnum->GetNameStringByValue((int64)SA->Attenuation.AttenuationShape.GetValue())
			: FString::FromInt((int32)SA->Attenuation.AttenuationShape.GetValue());

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             SA->GetName());
		Entry->SetStringField(TEXT("Path"),             SA->GetPathName());
		Entry->SetStringField(TEXT("Shape"),            ShapeStr);
		Entry->SetNumberField(TEXT("ShapeValue"),       (double)(int32)SA->Attenuation.AttenuationShape.GetValue());
		Entry->SetNumberField(TEXT("InnerRadius"),      SA->Attenuation.AttenuationShapeExtents.X);
		Entry->SetNumberField(TEXT("FalloffDistance"),  SA->Attenuation.FalloffDistance);
		Entry->SetBoolField(TEXT("bAttenuate"),         SA->Attenuation.bAttenuate != 0);
		Entry->SetBoolField(TEXT("bSpatialize"),        SA->Attenuation.bSpatialize != 0);
		Entry->SetNumberField(TEXT("DBAtMax"),          SA->Attenuation.dBAttenuationAtMax);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSoundAttenuationCapability)
