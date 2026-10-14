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

void FNexusInteractRuntimeActorNiagaraCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeActorNiagara");
	Out.Description = TEXT("Activate/deactivate runtime Niagara. Action=activate|deactivate.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("Activate"), TEXT("Deactivate") }))
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor name")))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Optional NiagaraSystem path (spawn on activate)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle") };
	Out.RelatedCapabilities = { TEXT("GetAssetNiagaraSystem") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Toggle Niagara in PIE or spawn by asset path");
}

FCapabilityResult FNexusInteractRuntimeActorNiagaraCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("Action"));
		FString ActorName, AssetPath;
		ActorName = FNexusArgs(Arguments).Str(TEXT("ActorName"), ActorName);
		AssetPath = FNexusArgs(Arguments).Str(TEXT("AssetPath"), AssetPath);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Action"), Action);

		AActor* Actor = nullptr;
		if (!ActorName.IsEmpty())
		{
			Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}

		if (Action.Equals(TEXT("Activate"), ESearchCase::IgnoreCase))
		{
			if (!AssetPath.IsEmpty())
			{
				UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
				if (!Sys)
				{
					Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				UNiagaraComponent* Comp = Actor
					? UNiagaraFunctionLibrary::SpawnSystemAttached(Sys, Actor->GetRootComponent(), NAME_None,
						FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true)
					: UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sys, FVector::ZeroVector);
				if (!Comp)
				{
					Entry->SetStringField(TEXT("Error"), TEXT("SpawnSystem failed"));
				}
				else
				{
					Entry->SetBoolField(TEXT("bActivated"), true);
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
				Entry->SetNumberField(TEXT("ComponentCount"), Comps.Num());
				Entry->SetBoolField(TEXT("bActivated"), true);
			}
			else
			{
				Entry->SetStringField(TEXT("Error"), TEXT("activate requires ActorName or AssetPath"));
			}
		}
		else if (Action.Equals(TEXT("Deactivate"), ESearchCase::IgnoreCase))
		{
			if (!Actor)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("deactivate requires ActorName"));
			}
			else
			{
				TArray<UNiagaraComponent*> Comps;
				Actor->GetComponents(Comps);
				for (UNiagaraComponent* C : Comps)
				{
					if (C) C->Deactivate();
				}
				Entry->SetBoolField(TEXT("bDeactivated"), true);
			}
		}
		else
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Only supports activate/deactivate"));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorNiagaraCapability)

#endif
