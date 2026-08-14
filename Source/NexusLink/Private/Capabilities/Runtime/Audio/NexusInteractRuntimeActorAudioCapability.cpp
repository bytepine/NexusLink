// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Audio/NexusInteractRuntimeActorAudioCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "NexusMcpTool.h"

void FInteractRuntimeActorAudioCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_audio");
	Out.Description = TEXT("运行时播放音效。action=play_sound；可选附着 Actor。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("play_sound") }))
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundCue/SoundWave 路径")))
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("可选：附着到该 Actor")))
		.Required({ TEXT("action"), TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("audio"), TEXT("play"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_cue"), TEXT("get_asset_sound_wave") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("PIE 中播放音效，可附着到 Actor");
}

FCapabilityResult FInteractRuntimeActorAudioCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString Action, AssetPath, ActorName;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}
		Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
		Arguments->TryGetStringField(TEXT("actorName"), ActorName);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("action"), Action);
		if (!Action.Equals(TEXT("play_sound"), ESearchCase::IgnoreCase))
		{
			Entry->SetStringField(TEXT("error"), TEXT("仅支持 play_sound"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		if (AssetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("play_sound 需要 assetPath"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *AssetPath);
		if (!Sound)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("音频资产未找到: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		if (!ActorName.IsEmpty())
		{
			AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor 未找到: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UGameplayStatics::SpawnSoundAttached(Sound, Actor->GetRootComponent());
			Entry->SetStringField(TEXT("actorName"), Actor->GetName());
		}
		else
		{
			UGameplayStatics::PlaySound2D(World, Sound);
		}
		Entry->SetStringField(TEXT("sound"), Sound->GetName());
		Entry->SetBoolField(TEXT("played"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorAudioCapability)
