// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FGetAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_sound_submix");
	Out.SearchAssetTypes = {TEXT("SoundSubmix")};
	Out.Description = TEXT("Read SoundSubmix: outputVolume/wetLevel/dryLevel/effectChainCount/parentSubmix.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundSubmix asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("sound"), TEXT("audio"), TEXT("volume"), TEXT("effects") };
	Out.RelatedCapabilities = { TEXT("manage_asset_sound_submix"), TEXT("create_asset_sound_submix"), TEXT("search_asset") };
}

FCapabilityResult FGetAssetSoundSubmixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		USoundSubmix* SM = LoadObject<USoundSubmix>(nullptr, *AssetPath);
		if (!SM)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load SoundSubmix: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             SM->GetName());
		Entry->SetStringField(TEXT("path"),             SM->GetPathName());

		// UE4.x / UE5.0: 线性 [0,1] 字段；UE5.1+ 改为 dB 调制目标
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
		Entry->SetNumberField(TEXT("outputVolume"),     SM->OutputVolume);
		Entry->SetNumberField(TEXT("wetLevel"),         SM->WetLevel);
		Entry->SetNumberField(TEXT("dryLevel"),         SM->DryLevel);
#else
		Entry->SetNumberField(TEXT("outputVolumeDB"),   SM->OutputVolumeModulation.Value);
		Entry->SetNumberField(TEXT("wetLevelDB"),       SM->WetLevelModulation.Value);
		Entry->SetNumberField(TEXT("dryLevelDB"),       SM->DryLevelModulation.Value);
#endif
		Entry->SetNumberField(TEXT("effectChainCount"), SM->SubmixEffectChain.Num());

		if (USoundSubmixBase* Parent = SM->ParentSubmix)
			Entry->SetStringField(TEXT("parentSubmix"), Parent->GetPathName());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundSubmixCapability)
