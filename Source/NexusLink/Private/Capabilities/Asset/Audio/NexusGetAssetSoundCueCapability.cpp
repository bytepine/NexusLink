// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundCueCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("检查 SoundCue 快照。时长/节点树摘要。写用 manage_asset_sound_cue。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundCue 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 SoundCue 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("cue"), TEXT("sound"), TEXT("node"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("manage_asset_sound_cue"), TEXT("search_asset"), TEXT("get_asset_sound_wave"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("读 Cue 节点树；根属性写用 manage_asset_sound_cue");
}

static void CollectSoundCuePaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		OutPaths.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
			{
				OutPaths.AddUnique(P);
			}
		}
	}
}

FCapabilityResult FGetAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectSoundCuePaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path);

			USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(Path);
			if (!Cue)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("SoundCue 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
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
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundCueCapability)
