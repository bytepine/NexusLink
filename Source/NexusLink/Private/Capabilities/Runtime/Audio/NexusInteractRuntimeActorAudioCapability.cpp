// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Audio/NexusInteractRuntimeActorAudioCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "NexusMcpTool.h"

void FInteractRuntimeActorAudioCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_audio");
	Out.Description = TEXT("Play sound at runtime. action=play_sound; optional attach Actor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("play_sound") }))
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundCue/SoundWave path")))
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Optional: attach to this Actor")))
		.Required({ TEXT("action"), TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("audio"), TEXT("play"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_cue"), TEXT("get_asset_sound_wave") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("Play sound in PIE; can attach to Actor");
}

FCapabilityResult FInteractRuntimeActorAudioCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("action"));
		FString AssetPath, ActorName;
		Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
		Arguments->TryGetStringField(TEXT("actorName"), ActorName);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("action"), Action);
		if (!Action.Equals(TEXT("play_sound"), ESearchCase::IgnoreCase))
		{
			Entry->SetStringField(TEXT("error"), TEXT("Only supports play_sound"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		if (AssetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("play_sound requires assetPath"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *AssetPath);
		if (!Sound)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Audio asset not found: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		if (!ActorName.IsEmpty())
		{
			AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
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
