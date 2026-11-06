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
#include "AI/Navigation/NavAgentInterface.h"

static bool ReadNavigationPoint(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Name, FVector& Point)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Arguments->TryGetObjectField(Name, Object) || !Object || !Object->IsValid()) return false;
	const FNexusArgs Args(*Object);
	const double X = Args.Num(TEXT("X")), Y = Args.Num(TEXT("Y")), Z = Args.Num(TEXT("Z"));
	if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
		|| FMath::Abs(X) > MAX_flt || FMath::Abs(Y) > MAX_flt || FMath::Abs(Z) > MAX_flt) return false;
	Point = FVector(X, Y, Z);
	return true;
}

static TSharedRef<FJsonObject> NavigationPointJson(const FVector& Point)
{
	auto Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("X"), Point.X);
	Object->SetNumberField(TEXT("Y"), Point.Y);
	Object->SetNumberField(TEXT("Z"), Point.Z);
	return Object;
}

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
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Optional exact actor name or unambiguous label for navigation-agent context; ordinary Actors are supported")))
		.Prop(TEXT("bAllowPartialPath"), FNexusSchema::Bool(TEXT("Allow a path that does not reach End"), true, false))
		.Prop(TEXT("MaxPoints"), FNexusSchema::Int(TEXT("Maximum returned path points; full path length/cost are retained"), 256, 1, 2048))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime};
	Out.ExtraSearchKeywords = {TEXT("navigation"), TEXT("navmesh"), TEXT("route"), TEXT("pathfinding"), TEXT("patrol")};
	Out.RelatedCapabilities = {TEXT("ListRuntimeActors"), TEXT("GetAssetLevel"), TEXT("InteractRuntimeActorAI")};
}

FCapabilityResult FNexusGetNavigationPathCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FVector Start, End;
	if (!ReadNavigationPoint(Arguments, TEXT("Start"), Start) || !ReadNavigationPoint(Arguments, TEXT("End"), End))
		return FCapabilityResult::MakeArgInvalid(TEXT("Start and End must contain three finite world coordinates"));
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		const int32 WorldMode = Args.EnumInt(TEXT("World"), {TEXT("Auto"), TEXT("Editor"), TEXT("Play")}, 0);
		UWorld* World = WorldMode == 2 ? FNexusRuntimeUtils::RequirePlayWorld(Error) : FNexusRuntimeUtils::GetActiveWorld();
		if (WorldMode == 1)
		{
			World = nullptr;
			if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (Context.WorldType == EWorldType::Editor && Context.World()) { World = Context.World(); break; }
		}
		if (!World) { Error = TEXT("No world of the requested type is available"); return; }
		AActor* Actor = nullptr;
		const FString ActorName = Args.Str(TEXT("ActorName"));
		if (!ActorName.IsEmpty())
		{
			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				if (ActorIt->GetName() != ActorName && FNexusRuntimeUtils::GetActorLabelOrName(*ActorIt) != ActorName) continue;
				if (Actor) { Error = TEXT("ActorName is ambiguous; use an exact actor name"); return; }
				Actor = *ActorIt;
			}
			if (!Actor) { Error = TEXT("ActorName was not found in the selected world"); return; }
		}
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("WorldName"), World->GetName());
		Entry->SetStringField(TEXT("WorldType"), World->WorldType == EWorldType::Editor ? TEXT("Editor") : (World->WorldType == EWorldType::PIE ? TEXT("PIE") : TEXT("Game")));
		Entry->SetObjectField(TEXT("Start"), NavigationPointJson(Start));
		Entry->SetObjectField(TEXT("End"), NavigationPointJson(End));
		if (Actor) Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const ANavigationData* NavData = Navigation ? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
		if (Navigation && Actor)
			if (const INavAgentInterface* Agent = Cast<INavAgentInterface>(Actor))
				NavData = Navigation->GetNavDataForProps(Agent->GetNavAgentPropertiesRef(), Start);
		Entry->SetBoolField(TEXT("bNavigationAvailable"), NavData != nullptr);
		Entry->SetBoolField(TEXT("bPathFound"), false);
		Entry->SetArrayField(TEXT("Points"), {});
		if (!Navigation || !NavData)
		{
			Entry->SetStringField(TEXT("QueryResult"), TEXT("Unavailable"));
			Entry->SetStringField(TEXT("Hint"), TEXT("No existing navigation data for this world/agent. This query never creates or rebuilds navigation."));
		}
		else
		{
			UObject* Querier = Actor ? static_cast<UObject*>(Actor) : static_cast<UObject*>(World);
			FPathFindingQuery Query(Querier, *NavData, Start, End);
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
					Values.Add(MakeShared<FJsonValueObject>(NavigationPointJson(Points[PointIndex].Location)));
				Entry->SetArrayField(TEXT("Points"), Values);
			}
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetNavigationPathCapability)
