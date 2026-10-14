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

void FNexusInteractRuntimeActorAudioCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeActorAudio");
	Out.Description = TEXT("Play sound at runtime. Action=PlaySound; optional attach Actor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("PlaySound") }))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SoundCue/SoundWave path")))
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Optional: attach to this Actor")))
		.Required({ TEXT("Action"), TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("Sound"), TEXT("audio"), TEXT("play"), TEXT("sfx") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundCue"), TEXT("GetAssetSoundWave") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Play sound in PIE; can attach to Actor");
}

FCapabilityResult FNexusInteractRuntimeActorAudioCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("Action"));
		FString AssetPath, ActorName;
		AssetPath = FNexusArgs(Arguments).Str(TEXT("AssetPath"), AssetPath);
		ActorName = FNexusArgs(Arguments).Str(TEXT("ActorName"), ActorName);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Action"), Action);
		if (!Action.Equals(TEXT("PlaySound"), ESearchCase::IgnoreCase))
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Only supports PlaySound"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		if (AssetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("PlaySound requires AssetPath"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *AssetPath);
		if (!Sound)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Audio asset not found: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		if (!ActorName.IsEmpty())
		{
			AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UGameplayStatics::SpawnSoundAttached(Sound, Actor->GetRootComponent());
			Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
		}
		else
		{
			UGameplayStatics::PlaySound2D(World, Sound);
		}
		Entry->SetStringField(TEXT("Sound"), Sound->GetName());
		Entry->SetBoolField(TEXT("bPlayed"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorAudioCapability)
