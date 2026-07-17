// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundWaveCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FGetAssetSoundWaveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_sound_wave");
	Out.SearchAssetTypes = {TEXT("SoundWave")};
	Out.Description = TEXT("检查 SoundWave 快照。时长/采样率/声道。写用 manage_asset_sound_wave。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundWave 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 SoundWave 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("sound"), TEXT("wave"), TEXT("sfx"), TEXT("duration") };
	Out.RelatedCapabilities = { TEXT("manage_asset_sound_wave"), TEXT("search_asset"), TEXT("get_asset_sound_cue"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("读波形元数据；写用 manage_asset_sound_wave");
}

static void CollectSoundWavePaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
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

FCapabilityResult FGetAssetSoundWaveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectSoundWavePaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path);

			USoundWave* Wave = FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(Path);
			if (!Wave)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("SoundWave 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), Wave->GetName());
			Entry->SetStringField(TEXT("assetType"), TEXT("SoundWave"));
			Entry->SetNumberField(TEXT("duration"), Wave->GetDuration());
			Entry->SetNumberField(TEXT("sampleRate"), Wave->GetSampleRateForCurrentPlatform());
			Entry->SetNumberField(TEXT("numChannels"), Wave->NumChannels);
			Entry->SetNumberField(TEXT("totalSamples"), static_cast<double>(Wave->TotalSamples));
			Entry->SetBoolField(TEXT("bStreaming"), Wave->bStreaming);
			Entry->SetBoolField(TEXT("bLooping"), Wave->bLooping);

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundWaveCapability)
