// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/AI/NexusInteractRuntimeActorAICapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "NexusMcpTool.h"

void FNexusInteractRuntimeActorAICapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeActorAI");
	Out.Description = TEXT("runtime AI Move. Action=MoveTo; requires AIController.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("MoveTo") }))
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Pawn/Actor name")))
		.Prop(TEXT("x"), FNexusSchema::Num(TEXT("Target X")))
		.Prop(TEXT("y"), FNexusSchema::Num(TEXT("Target Y")))
		.Prop(TEXT("z"), FNexusSchema::Num(TEXT("Target Z")))
		.Required({ TEXT("Action"), TEXT("ActorName") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("ai"), TEXT("move"), TEXT("Path"), TEXT("Controller") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorBehaviorTree"), TEXT("ListRuntimeActors") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Move AIController Pawn to coordinates in PIE");
}

FCapabilityResult FNexusInteractRuntimeActorAICapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("Action"));
		FString ActorName;
		ActorName = FNexusArgs(Arguments).Str(TEXT("ActorName"), ActorName);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Action"), Action);
		if (!Action.Equals(TEXT("MoveTo"), ESearchCase::IgnoreCase))
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Only supports MoveTo"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		AAIController* AIC = Cast<AAIController>(Actor);
		if (!AIC)
		{
			if (APawn* Pawn = Cast<APawn>(Actor))
			{
				AIC = Cast<AAIController>(Pawn->GetController());
			}
		}
		if (!AIC)
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Target has no AIController"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}
		double X = 0, Y = 0, Z = 0;
		Arguments->TryGetNumberField(TEXT("x"), X);
		Arguments->TryGetNumberField(TEXT("y"), Y);
		Arguments->TryGetNumberField(TEXT("z"), Z);
		const FVector Dest(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
		const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(Dest);
		Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
		Entry->SetNumberField(TEXT("MoveResult"), static_cast<int32>(Result));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorAICapability)
