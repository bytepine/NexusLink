// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSoundSubmix");
	Out.SearchAssetTypes = {TEXT("SoundSubmix")};
	Out.Description = TEXT("Read SoundSubmix: OutputVolume/WetLevel/DryLevel/EffectChainCount/ParentSubmix.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundSubmix asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("Sound"), TEXT("audio"), TEXT("Volume"), TEXT("Effects") };
	Out.RelatedCapabilities = { TEXT("ManageAssetSoundSubmix"), TEXT("CreateAssetSoundSubmix"), TEXT("SearchAsset") };
}

FCapabilityResult FNexusGetAssetSoundSubmixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		USoundSubmix* SM = LoadObject<USoundSubmix>(nullptr, *AssetPath);
		if (!SM)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load SoundSubmix: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             SM->GetName());
		Entry->SetStringField(TEXT("Path"),             SM->GetPathName());

		// UE4.x / UE5.0: 线性 [0,1] 字段；UE5.1+ 改为 dB 调制目标
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
		Entry->SetNumberField(TEXT("OutputVolume"),     SM->OutputVolume);
		Entry->SetNumberField(TEXT("WetLevel"),         SM->WetLevel);
		Entry->SetNumberField(TEXT("DryLevel"),         SM->DryLevel);
#else
		Entry->SetNumberField(TEXT("OutputVolumeDB"),   SM->OutputVolumeModulation.Value);
		Entry->SetNumberField(TEXT("WetLevelDB"),       SM->WetLevelModulation.Value);
		Entry->SetNumberField(TEXT("DryLevelDB"),       SM->DryLevelModulation.Value);
#endif
		Entry->SetNumberField(TEXT("EffectChainCount"), SM->SubmixEffectChain.Num());

		if (USoundSubmixBase* Parent = SM->ParentSubmix)
			Entry->SetStringField(TEXT("ParentSubmix"), Parent->GetPathName());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSoundSubmixCapability)
