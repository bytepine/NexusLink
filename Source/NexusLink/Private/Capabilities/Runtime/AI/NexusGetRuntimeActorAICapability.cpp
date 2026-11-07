// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/AI/NexusGetRuntimeActorAICapability.h"
#include "Utils/NexusAIUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "AIController.h"

void FNexusGetRuntimeActorAICapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeActorAI");
	Out.Description = TEXT("Inspect controller movement state and captured request outcome. Acceptance alone does not prove arrival.");
	Out.InputSchema = FNexusSchema::Object().Required(TEXT("ActorName"), FNexusSchema::Str(TEXT("Pawn or AIController name from ListRuntimeActors"))).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime};
	Out.RelatedCapabilities = {TEXT("InteractRuntimeActorAI"), TEXT("GetNavigationPath"), TEXT("GetRuntimeActorBehaviorTree")};
	Out.Prerequisites = {TEXT("PIE")};
	Out.ExtraSearchKeywords = {TEXT("ai"), TEXT("move"), TEXT("status"), TEXT("arrival"), TEXT("path following")};
}

FCapabilityResult FNexusGetRuntimeActorAICapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(Error);
		if (!World) return;
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, FNexusArgs(Arguments).Str(TEXT("ActorName")));
		if (!Actor) { Error = TEXT("Actor not found"); return; }
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
		AAIController* Controller = FNexusAIUtils::ResolveController(Actor);
		Entry->SetBoolField(TEXT("bHasAIController"), Controller != nullptr);
		if (Controller) FNexusAIUtils::Describe(Controller, Entry);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}
REGISTER_MCP_CAPABILITY(FNexusGetRuntimeActorAICapability)
