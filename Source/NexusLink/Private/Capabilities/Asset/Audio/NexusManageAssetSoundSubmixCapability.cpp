// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FManageAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_submix");
	Out.SearchAssetTypes = {TEXT("SoundSubmix")};
	Out.Description = TEXT("Set SoundSubmix volume (UE5.1+ uses dB fields; see InputSchema).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("outputVolume"),   FNexusSchema::Num(TEXT("Output volume linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("wetLevel"),       FNexusSchema::Num(TEXT("Wet level linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("dryLevel"),       FNexusSchema::Num(TEXT("Dry level linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("outputVolumeDB"), FNexusSchema::Num(TEXT("Output volume dB [-96,0] (UE5.1+)")))
		.Prop(TEXT("wetLevelDB"),     FNexusSchema::Num(TEXT("Wet level dB [-96,0] (UE5.1+)")))
		.Prop(TEXT("dryLevelDB"),     FNexusSchema::Num(TEXT("Dry level dB [-96,0] (UE5.1+)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundSubmix asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("sound"), TEXT("audio"), TEXT("volume"), TEXT("wet"), TEXT("dry") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_submix"), TEXT("create_asset_sound_submix") };
}

FCapabilityResult FManageAssetSoundSubmixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		USoundSubmix* SM = LoadObject<USoundSubmix>(nullptr, *AssetPath);
		if (!SM)
		{
			OutError = FString::Printf(TEXT("Failed to load SoundSubmix: %s"), *AssetPath);
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

#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
			if (Op->HasField(TEXT("outputVolume"))) SM->OutputVolume = FMath::Clamp((float)Op->GetNumberField(TEXT("outputVolume")), 0.f, 1.f);
			if (Op->HasField(TEXT("wetLevel")))     SM->WetLevel     = FMath::Clamp((float)Op->GetNumberField(TEXT("wetLevel")),     0.f, 1.f);
			if (Op->HasField(TEXT("dryLevel")))     SM->DryLevel     = FMath::Clamp((float)Op->GetNumberField(TEXT("dryLevel")),     0.f, 1.f);
#else
			if (Op->HasField(TEXT("outputVolumeDB"))) SM->OutputVolumeModulation.Value = FMath::Clamp((float)Op->GetNumberField(TEXT("outputVolumeDB")), -96.f, 0.f);
			if (Op->HasField(TEXT("wetLevelDB")))     SM->WetLevelModulation.Value     = FMath::Clamp((float)Op->GetNumberField(TEXT("wetLevelDB")),     -96.f, 0.f);
			if (Op->HasField(TEXT("dryLevelDB")))     SM->DryLevelModulation.Value     = FMath::Clamp((float)Op->GetNumberField(TEXT("dryLevelDB")),     -96.f, 0.f);
#endif

			SM->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"), SM->GetName());
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
			Entry->SetNumberField(TEXT("outputVolume"), SM->OutputVolume);
			Entry->SetNumberField(TEXT("wetLevel"),     SM->WetLevel);
			Entry->SetNumberField(TEXT("dryLevel"),     SM->DryLevel);
#else
			Entry->SetNumberField(TEXT("outputVolumeDB"), SM->OutputVolumeModulation.Value);
			Entry->SetNumberField(TEXT("wetLevelDB"),     SM->WetLevelModulation.Value);
			Entry->SetNumberField(TEXT("dryLevelDB"),     SM->DryLevelModulation.Value);
#endif
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundSubmixCapability)
