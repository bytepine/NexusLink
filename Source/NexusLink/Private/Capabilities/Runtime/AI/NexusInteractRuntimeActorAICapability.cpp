// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/AI/NexusInteractRuntimeActorAICapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAIUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavFilters/NavigationQueryFilter.h"

void FNexusInteractRuntimeActorAICapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("InteractRuntimeActorAI");
    Out.Description = TEXT("Request controller movement or stop it. Query GetRuntimeActorAI for completion; acceptance is not arrival.");
    Out.InputSchema = FNexusSchema::Object()
        .Required(TEXT("Action"), FNexusSchema::Enum(TEXT("AI movement command"), {TEXT("MoveTo"), TEXT("Stop")}))
        .Required(TEXT("ActorName"), FNexusSchema::Str(TEXT("Pawn or AIController name")))
        .Prop(TEXT("X"), FNexusSchema::Num(TEXT("MoveTo destination X")))
        .Prop(TEXT("Y"), FNexusSchema::Num(TEXT("MoveTo destination Y")))
        .Prop(TEXT("Z"), FNexusSchema::Num(TEXT("MoveTo destination Z")))
        .Prop(TEXT("AcceptanceRadius"), FNexusSchema::Num(TEXT("MoveTo acceptance radius; -1 uses the controller default")))
        .Prop(TEXT("FilterClass"), FNexusSchema::Str(TEXT("NavigationQueryFilter class; align with GetNavigationPath")))
        .Prop(TEXT("bAllowPartialPath"), FNexusSchema::Bool(TEXT("Allow partial movement paths; explicitly align this with path queries"), true, true))
        .Prop(TEXT("bProjectEnd"), FNexusSchema::Bool(TEXT("Project destination onto navigation"), true, true)).Build();
    Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime};
    Out.bAutoTransaction = false;
    Out.bAllowBatch = false;
    Out.ExtraSearchKeywords = {TEXT("ai"), TEXT("move"), TEXT("stop"), TEXT("path"), TEXT("controller")};
    Out.RelatedCapabilities = {TEXT("GetRuntimeActorAI"), TEXT("GetNavigationPath"), TEXT("GetNavigationPoint"), TEXT("GetRuntimeActorBehaviorTree"), TEXT("ListRuntimeActors")};
    Out.Prerequisites = {TEXT("PIE")};
}

FCapabilityResult FNexusInteractRuntimeActorAICapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
    {
        const FNexusArgs Args(Arguments);
        UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(Error);
        if (!World) return;
        AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, Args.Str(TEXT("ActorName")));
        if (!Actor) { Error = TEXT("Actor not found"); return; }
        AAIController* Controller = FNexusAIUtils::ResolveController(Actor);
        if (!Controller) { Error = TEXT("Target has no AIController"); return; }
        const bool bStop = Args.Str(TEXT("Action")).Equals(TEXT("Stop"), ESearchCase::IgnoreCase);
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("Action"), bStop ? TEXT("Stop") : TEXT("MoveTo"));
        Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
        if (bStop) Controller->StopMovement();
        else
        {
            double X = 0, Y = 0, Z = 0;
            const double Radius = Args.Num(TEXT("AcceptanceRadius"), -1);
            if ((Arguments->HasField(TEXT("X")) && !Arguments->TryGetNumberField(TEXT("X"), X))
                || (Arguments->HasField(TEXT("Y")) && !Arguments->TryGetNumberField(TEXT("Y"), Y))
                || (Arguments->HasField(TEXT("Z")) && !Arguments->TryGetNumberField(TEXT("Z"), Z))
                || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z) || !FMath::IsFinite(Radius)
                || FMath::Abs(X) > MAX_flt || FMath::Abs(Y) > MAX_flt || FMath::Abs(Z) > MAX_flt || Radius < -1 || Radius > MAX_flt)
            { Error = TEXT("MoveTo requires finite supplied coordinates (omitted axes default to zero) and AcceptanceRadius >= -1"); return; }
            UClass* Filter = nullptr;
            if (!Args.Str(TEXT("FilterClass")).IsEmpty())
            {
                Filter = FNexusAssetUtils::FindClassWithUPrefix(Args.Str(TEXT("FilterClass")));
                if (!Filter || !Filter->IsChildOf(UNavigationQueryFilter::StaticClass())) { Error = TEXT("FilterClass must derive from NavigationQueryFilter"); return; }
            }
            FNexusAIUtils::BeginMove(Controller);
            const EPathFollowingRequestResult::Type Result = Controller->MoveToLocation(FVector(X, Y, Z), static_cast<float>(Radius), true, true, Args.Bool(TEXT("bProjectEnd"), true), true, Filter, Args.Bool(TEXT("bAllowPartialPath"), true));
            FNexusAIUtils::AcceptMove(Controller, static_cast<int32>(Result));
            Entry->SetNumberField(TEXT("MoveResult"), static_cast<int32>(Result));
            Entry->SetStringField(TEXT("MoveResultName"), Result == EPathFollowingRequestResult::RequestSuccessful ? TEXT("RequestSuccessful") : (Result == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("AlreadyAtGoal") : TEXT("Failed")));
        }
        FNexusAIUtils::Describe(Controller, Entry);
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    });
}
REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorAICapability)
