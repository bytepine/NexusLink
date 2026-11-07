// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/AI/NexusGetNavigationPathCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationSystemTypes.h"
#include "Utils/NexusNavigationUtils.h"
#include "AI/Navigation/NavAgentInterface.h"

void FNexusGetNavigationPathCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetNavigationPath");
	Out.Description = TEXT("Read-only navigation path in Editor/PIE/Game using existing nav data. No movement, AIController or nav rebuild.");
	const TSharedPtr<FJsonObject> Point = FNexusSchema::Object()
		.Required(TEXT("X"), FNexusSchema::Num(TEXT("World X")))
		.Required(TEXT("Y"), FNexusSchema::Num(TEXT("World Y")))
		.Required(TEXT("Z"), FNexusSchema::Num(TEXT("World Z"))).Build();
	Point->SetStringField(TEXT("description"), TEXT("World coordinates {X:number,Y:number,Z:number}; all three fields are required"));
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Start"), Point.ToSharedRef())
		.Required(TEXT("End"), Point.ToSharedRef())
		.Prop(TEXT("World"), FNexusSchema::Enum(TEXT("World selection; Auto prefers PIE/Game, otherwise Editor"), {TEXT("Auto"), TEXT("Editor"), TEXT("Play")}, TEXT("Auto")))
		.Prop(TEXT("WorldName"), FNexusSchema::Str(TEXT("Optional exact world name; disambiguates multiple PIE worlds")))
        .Prop(TEXT("FilterClass"), FNexusSchema::Str(TEXT("Optional NavigationQueryFilter class name/path")))
        .Prop(TEXT("AgentRadius"), FNexusSchema::Num(TEXT("Optional navigation-agent radius override")))
        .Prop(TEXT("AgentHeight"), FNexusSchema::Num(TEXT("Optional navigation-agent height override")))
        .Prop(TEXT("ProjectionExtent"), Point.ToSharedRef())
        .Prop(TEXT("bProjectStart"), FNexusSchema::Bool(TEXT("Project Start onto existing navigation before querying"), true, false))
        .Prop(TEXT("bProjectEnd"), FNexusSchema::Bool(TEXT("Project End onto existing navigation before querying"), true, false))
        .Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Optional exact actor name or unambiguous label for navigation-agent context; ordinary Actors are supported")))
		.Prop(TEXT("bAllowPartialPath"), FNexusSchema::Bool(TEXT("Allow a path that does not reach End"), true, false))
		.Prop(TEXT("MaxPoints"), FNexusSchema::Int(TEXT("Maximum returned path points; full path length/cost are retained"), 256, 1, 2048))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime};
	Out.ExtraSearchKeywords = {TEXT("navigation"), TEXT("navmesh"), TEXT("route"), TEXT("pathfinding"), TEXT("patrol"), TEXT("projection"), TEXT("filter"), TEXT("agent")};
	Out.RelatedCapabilities = {TEXT("ListRuntimeActors"), TEXT("GetAssetLevel"), TEXT("InteractRuntimeActorAI"), TEXT("GetNavigationPoint"), TEXT("GetRuntimeActorAI")};
}

FCapabilityResult FNexusGetNavigationPathCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FVector Start, End;
	if (!FNexusNavigationUtils::ReadPoint(Arguments, TEXT("Start"), Start) || !FNexusNavigationUtils::ReadPoint(Arguments, TEXT("End"), End))
		return FCapabilityResult::MakeArgInvalid(TEXT("Start and End must contain three finite world coordinates"));
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
        FNexusNavigationContext Context;
        if (!FNexusNavigationUtils::Resolve(Arguments, Start, Context, Error)) return;
        auto Entry = MakeShared<FJsonObject>();
        FNexusNavigationUtils::Describe(Context, Entry);
        Entry->SetObjectField(TEXT("Start"), FNexusNavigationUtils::PointJson(Start));
        Entry->SetObjectField(TEXT("End"), FNexusNavigationUtils::PointJson(End));
        UNavigationSystemV1* Navigation = Context.System;
        const ANavigationData* NavData = Context.Data;
		Entry->SetBoolField(TEXT("bPathFound"), false);
		Entry->SetArrayField(TEXT("Points"), {});
		if (!Navigation || !NavData)
		{
			Entry->SetStringField(TEXT("QueryResult"), TEXT("Unavailable"));
			Entry->SetStringField(TEXT("Hint"), TEXT("No existing navigation data for this world/agent. This query never creates or rebuilds navigation."));
		}
		else
		{
			FVector QueryStart = Start, QueryEnd = End;
            FNavLocation Projected;
            if (Args.Bool(TEXT("bProjectStart")))
            {
                const bool bProjected = Navigation->ProjectPointToNavigation(Start, Projected, Context.Extent, NavData, Context.Filter);
                Entry->SetBoolField(TEXT("bStartProjected"), bProjected);
                if (!bProjected) { Entry->SetStringField(TEXT("QueryResult"), TEXT("StartProjectionFailed")); Entries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
                QueryStart = Projected.Location;
            }
            if (Args.Bool(TEXT("bProjectEnd")))
            {
                const bool bProjected = Navigation->ProjectPointToNavigation(End, Projected, Context.Extent, NavData, Context.Filter);
                Entry->SetBoolField(TEXT("bEndProjected"), bProjected);
                if (!bProjected) { Entry->SetStringField(TEXT("QueryResult"), TEXT("EndProjectionFailed")); Entries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
                QueryEnd = Projected.Location;
            }
            Entry->SetObjectField(TEXT("QueryStart"), FNexusNavigationUtils::PointJson(QueryStart));
            Entry->SetObjectField(TEXT("QueryEnd"), FNexusNavigationUtils::PointJson(QueryEnd));
            UObject* Querier = Context.Actor ? static_cast<UObject*>(Context.Actor) : static_cast<UObject*>(Context.World);
            FPathFindingQuery Query(Querier, *NavData, QueryStart, QueryEnd, Context.Filter);
            Query.SetNavAgentProperties(Context.Agent);
			Query.SetAllowPartialPaths(Args.Bool(TEXT("bAllowPartialPath")));
			const FPathFindingResult Result = Navigation->FindPathSync(Query);
			Entry->SetStringField(TEXT("NavData"), NavData->GetPathName());
			Entry->SetStringField(TEXT("QueryResult"), Result.Result == ENavigationQueryResult::Success ? TEXT("Success") : (Result.Result == ENavigationQueryResult::Fail ? TEXT("Fail") : (Result.Result == ENavigationQueryResult::Error ? TEXT("Error") : TEXT("Invalid"))));
			Entry->SetBoolField(TEXT("bPathFound"), Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->IsValid()
				&& (Args.Bool(TEXT("bAllowPartialPath")) || !Result.Path->IsPartial()));
			if (Result.Path.IsValid())
			{
				const auto& Points = Result.Path->GetPathPoints();
				Entry->SetBoolField(TEXT("bPartial"), Result.Path->IsPartial());
				Entry->SetNumberField(TEXT("Length"), Result.Path->GetLength());
				Entry->SetNumberField(TEXT("Cost"), Result.Path->GetCost());
				Entry->SetNumberField(TEXT("PointCount"), Points.Num());
				const int32 Limit = static_cast<int32>(Args.Num(TEXT("MaxPoints"), 256));
				Entry->SetBoolField(TEXT("bTruncated"), Points.Num() > Limit);
				TArray<TSharedPtr<FJsonValue>> Values;
				for (int32 PointIndex = 0; PointIndex < FMath::Min(Points.Num(), Limit); ++PointIndex)
					Values.Add(MakeShared<FJsonValueObject>(FNexusNavigationUtils::PointJson(Points[PointIndex].Location)));
				Entry->SetArrayField(TEXT("Points"), Values);
			}
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetNavigationPathCapability)
