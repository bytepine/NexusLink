// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusDestroyRuntimeActorCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "NexusMcpTool.h"

void FNexusDestroyRuntimeActorCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("DestroyRuntimeActor");
	Out.Description = TEXT("Remove runtime Actor from PIE/Game; marks Level modified.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor name or tag to destroy")))
		.Required({ TEXT("ActorName") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("kill"), TEXT("despawn"), TEXT("Remove"), TEXT("drop"), TEXT("delete") };
	Out.RelatedCapabilities = { TEXT("SpawnRuntimeActor"), TEXT("ListRuntimeActors") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusDestroyRuntimeActorCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		const FString ActorName = A.Str(TEXT("ActorName"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ActorName"), ActorName);

		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!IsValid(Actor))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FString Name  = Actor->GetName();
		const FString Class = Actor->GetClass()->GetName();

	#if WITH_EDITOR
		const bool bDestroyed = Actor->Destroy();
		if (bDestroyed && World->GetCurrentLevel())
			World->GetCurrentLevel()->MarkPackageDirty();
	#else
		const bool bDestroyed = Actor->Destroy();
	#endif

		if (!bDestroyed)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Destroy failed: %s"), *Name));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),  Name);
		Entry->SetStringField(TEXT("Class"), Class);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusDestroyRuntimeActorCapability)
