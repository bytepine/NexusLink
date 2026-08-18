// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FManageAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_class");
	Out.SearchAssetTypes = {TEXT("SoundClass")};
	Out.Description = TEXT("Set SoundClass volume/pitch/lowPassFilter/attenuationScale.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("volume"),           FNexusSchema::Num(TEXT("Volume multiplier [0,∞)")))
		.Prop(TEXT("pitch"),            FNexusSchema::Num(TEXT("Pitch multiplier [0,∞)")))
		.Prop(TEXT("lowPassFilter"),    FNexusSchema::Num(TEXT("Low-pass cutoff (Hz)")))
		.Prop(TEXT("attenuationScale"), FNexusSchema::Num(TEXT("Attenuation distance scale")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundClass asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("class"), TEXT("volume"), TEXT("pitch"), TEXT("filter") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_class"), TEXT("create_asset_sound_class") };
}

FCapabilityResult FManageAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		USoundClass* SC = LoadObject<USoundClass>(nullptr, *AssetPath);
		if (!SC)
		{
			OutError = FString::Printf(TEXT("Failed to load SoundClass: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = FNexusArgs(Op).Str(TEXT("action")).ToLower();
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s' (set only)"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Op->HasField(TEXT("volume")))           SC->Properties.Volume                   = (float)Op->GetNumberField(TEXT("volume"));
			if (Op->HasField(TEXT("pitch")))            SC->Properties.Pitch                    = (float)Op->GetNumberField(TEXT("pitch"));
			if (Op->HasField(TEXT("lowPassFilter")))    SC->Properties.LowPassFilterFrequency   = (float)Op->GetNumberField(TEXT("lowPassFilter"));
			if (Op->HasField(TEXT("attenuationScale"))) SC->Properties.AttenuationDistanceScale = (float)Op->GetNumberField(TEXT("attenuationScale"));

			SC->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),          SC->GetName());
			Entry->SetNumberField(TEXT("volume"),        SC->Properties.Volume);
			Entry->SetNumberField(TEXT("pitch"),         SC->Properties.Pitch);
			Entry->SetNumberField(TEXT("lowPassFilter"), SC->Properties.LowPassFilterFrequency);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundClassCapability)
