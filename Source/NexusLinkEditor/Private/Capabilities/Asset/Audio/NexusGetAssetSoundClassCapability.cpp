// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSoundClass");
	Out.SearchAssetTypes = {TEXT("SoundClass")};
	Out.Description = TEXT("Read SoundClass: volume/pitch/LowPassFilter/ParentClass/ChildClasses.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundClass asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sound"), TEXT("Class"), TEXT("Volume"), TEXT("Pitch"), TEXT("Hierarchy") };
	Out.RelatedCapabilities = { TEXT("CreateAssetSoundClass"), TEXT("ManageAssetSoundClass") };
}

FCapabilityResult FNexusGetAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		USoundClass* SC = LoadObject<USoundClass>(nullptr, *AssetPath);
		if (!SC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load SoundClass: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),                SC->GetName());
		Entry->SetStringField(TEXT("Path"),                SC->GetPathName());
		Entry->SetNumberField(TEXT("Volume"),              SC->Properties.Volume);
		Entry->SetNumberField(TEXT("Pitch"),               SC->Properties.Pitch);
		Entry->SetNumberField(TEXT("LowPassFilter"),       SC->Properties.LowPassFilterFrequency);
		Entry->SetNumberField(TEXT("AttenuationScale"),    SC->Properties.AttenuationDistanceScale);
		Entry->SetNumberField(TEXT("ChildClassCount"),     SC->ChildClasses.Num());

		if (SC->ParentClass)
			Entry->SetStringField(TEXT("ParentClass"), SC->ParentClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> ChildArr;
		for (USoundClass* Child : SC->ChildClasses)
		{
			if (Child)
				ChildArr.Add(MakeShared<FJsonValueString>(Child->GetPathName()));
		}
		Entry->SetArrayField(TEXT("ChildClasses"), ChildArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSoundClassCapability)
