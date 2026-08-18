// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Niagara/NexusInteractRuntimeActorNiagaraCapability.h"

#if WITH_NIAGARA

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

void FInteractRuntimeActorNiagaraCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_niagara");
	Out.Description = TEXT("Activate/deactivate runtime Niagara. action=activate|deactivate.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("activate"), TEXT("deactivate") }))
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Actor name")))
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Optional NiagaraSystem path (spawn on activate)")))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle") };
	Out.RelatedCapabilities = { TEXT("get_asset_niagara_system") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("Toggle Niagara in PIE or spawn by asset path");
}

FCapabilityResult FInteractRuntimeActorNiagaraCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("action"));
		FString ActorName, AssetPath;
		Arguments->TryGetStringField(TEXT("actorName"), ActorName);
		Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("action"), Action);

		AActor* Actor = nullptr;
		if (!ActorName.IsEmpty())
		{
			Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}

		if (Action.Equals(TEXT("activate"), ESearchCase::IgnoreCase))
		{
			if (!AssetPath.IsEmpty())
			{
				UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
				if (!Sys)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				UNiagaraComponent* Comp = Actor
					? UNiagaraFunctionLibrary::SpawnSystemAttached(Sys, Actor->GetRootComponent(), NAME_None,
						FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true)
					: UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sys, FVector::ZeroVector);
				if (!Comp)
				{
					Entry->SetStringField(TEXT("error"), TEXT("SpawnSystem failed"));
				}
				else
				{
					Entry->SetBoolField(TEXT("activated"), true);
				}
			}
			else if (Actor)
			{
				TArray<UNiagaraComponent*> Comps;
				Actor->GetComponents(Comps);
				for (UNiagaraComponent* C : Comps)
				{
					if (C) C->Activate(true);
				}
				Entry->SetNumberField(TEXT("componentCount"), Comps.Num());
				Entry->SetBoolField(TEXT("activated"), true);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), TEXT("activate requires actorName or assetPath"));
			}
		}
		else if (Action.Equals(TEXT("deactivate"), ESearchCase::IgnoreCase))
		{
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), TEXT("deactivate requires actorName"));
			}
			else
			{
				TArray<UNiagaraComponent*> Comps;
				Actor->GetComponents(Comps);
				for (UNiagaraComponent* C : Comps)
				{
					if (C) C->Deactivate();
				}
				Entry->SetBoolField(TEXT("deactivated"), true);
			}
		}
		else
		{
			Entry->SetStringField(TEXT("error"), TEXT("Only supports activate/deactivate"));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorNiagaraCapability)

#endif
