// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundCueCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "NexusMcpTool.h"

void FGetAssetSoundCueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_sound_cue");
	Out.SearchAssetTypes = {TEXT("SoundCue")};
	Out.Description = TEXT("Inspect SoundCue snapshot. Writes via manage_asset_sound_cue.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundCue asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("cue"), TEXT("sound"), TEXT("node"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("manage_asset_sound_cue"), TEXT("create_asset_sound_cue"), TEXT("search_asset"), TEXT("get_asset_sound_wave"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("Read Cue node tree; root props via manage_asset_sound_cue");
}

FCapabilityResult FGetAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("assetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(Path);
		if (!Cue)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("SoundCue not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Cue->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("SoundCue"));
		Entry->SetNumberField(TEXT("duration"), Cue->GetDuration());
		Entry->SetNumberField(TEXT("maxDistance"), Cue->MaxDistance);

		TArray<USoundNode*> Nodes;
		if (Cue->FirstNode)
		{
			Cue->RecursiveFindAllNodes(Cue->FirstNode, Nodes);
		}
		Entry->SetNumberField(TEXT("nodeCount"), Nodes.Num());

		TArray<TSharedPtr<FJsonValue>> NodeArr;
		const int32 MaxNodes = FMath::Min(Nodes.Num(), 48);
		for (int32 i = 0; i < MaxNodes; ++i)
		{
			USoundNode* Node = Nodes[i];
			if (!Node) continue;
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetNumberField(TEXT("index"), static_cast<double>(i));
			NodeObj->SetStringField(TEXT("nodeType"), Node->GetClass()->GetName());
			if (USoundNodeWavePlayer* Player = Cast<USoundNodeWavePlayer>(Node))
			{
#if NX_UE_HAS_SOUND_NODE_WAVE_ACCESSOR
				if (USoundWave* Wave = Player->GetSoundWave())
				{
					NodeObj->SetStringField(TEXT("soundWave"), Wave->GetPathName());
				}
#endif
			}
			NodeArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		Entry->SetArrayField(TEXT("nodes"), NodeArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundCueCapability)
