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

void FNexusGetAssetSoundCueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSoundCue");
	Out.SearchAssetTypes = {TEXT("SoundCue")};
	Out.Description = TEXT("Inspect SoundCue snapshot. Writes via ManageAssetSoundCue.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundCue asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("cue"), TEXT("Sound"), TEXT("Node"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("ManageAssetSoundCue"), TEXT("CreateAssetSoundCue"), TEXT("SearchAsset"), TEXT("GetAssetSoundWave"), TEXT("GetAssetRefs") };
	Out.WhenToUse = TEXT("Read Cue node tree; root props via ManageAssetSoundCue");
}

FCapabilityResult FNexusGetAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(Path);
		if (!Cue)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("SoundCue not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Cue->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("SoundCue"));
		Entry->SetNumberField(TEXT("Duration"), Cue->GetDuration());
		Entry->SetNumberField(TEXT("MaxDistance"), Cue->MaxDistance);

		TArray<USoundNode*> Nodes;
		if (Cue->FirstNode)
		{
			Cue->RecursiveFindAllNodes(Cue->FirstNode, Nodes);
		}
		Entry->SetNumberField(TEXT("NodeCount"), Nodes.Num());

		TArray<TSharedPtr<FJsonValue>> NodeArr;
		const int32 MaxNodes = FMath::Min(Nodes.Num(), 48);
		for (int32 i = 0; i < MaxNodes; ++i)
		{
			USoundNode* Node = Nodes[i];
			if (!Node) continue;
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetNumberField(TEXT("Index"), static_cast<double>(i));
			NodeObj->SetStringField(TEXT("NodeType"), Node->GetClass()->GetName());
			if (USoundNodeWavePlayer* Player = Cast<USoundNodeWavePlayer>(Node))
			{
#if NX_UE_HAS_SOUND_NODE_WAVE_ACCESSOR
				if (USoundWave* Wave = Player->GetSoundWave())
				{
					NodeObj->SetStringField(TEXT("SoundWave"), Wave->GetPathName());
				}
#endif
			}
			NodeArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		Entry->SetArrayField(TEXT("Nodes"), NodeArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSoundCueCapability)
