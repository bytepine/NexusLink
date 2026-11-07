// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusNavigationUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "AI/Navigation/NavAgentInterface.h"

bool FNexusNavigationUtils::ReadPoint(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Name, FVector& Point)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Arguments || !Arguments->TryGetObjectField(Name, Object) || !Object || !Object->IsValid()) return false;
	double X = 0, Y = 0, Z = 0;
	if (!(*Object)->TryGetNumberField(TEXT("X"), X) || !(*Object)->TryGetNumberField(TEXT("Y"), Y) || !(*Object)->TryGetNumberField(TEXT("Z"), Z)
		|| !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
		|| FMath::Abs(X) > MAX_flt || FMath::Abs(Y) > MAX_flt || FMath::Abs(Z) > MAX_flt) return false;
	Point = FVector(X, Y, Z);
	return true;
}

TSharedRef<FJsonObject> FNexusNavigationUtils::PointJson(const FVector& Point)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("X"), Point.X);
	Result->SetNumberField(TEXT("Y"), Point.Y);
	Result->SetNumberField(TEXT("Z"), Point.Z);
	return Result;
}

bool FNexusNavigationUtils::Resolve(const TSharedPtr<FJsonObject>& Arguments, const FVector& Start, FNexusNavigationContext& Context, FString& Error)
{
	const FNexusArgs Args(Arguments);
	const int32 Mode = Args.EnumInt(TEXT("World"), {TEXT("Auto"), TEXT("Editor"), TEXT("Play")}, 0);
	Context.World = Mode == 2 ? FNexusRuntimeUtils::RequirePlayWorld(Error) : FNexusRuntimeUtils::GetActiveWorld();
	if (Mode == 1 || !Args.Str(TEXT("WorldName")).IsEmpty())
	{
		Context.World = nullptr;
		if (GEngine) for (const FWorldContext& World : GEngine->GetWorldContexts())
		{
			const bool bPlay = World.WorldType == EWorldType::PIE || World.WorldType == EWorldType::Game;
			if (!World.World() || (Mode == 1 ? World.WorldType != EWorldType::Editor : (Mode == 2 ? !bPlay : (!bPlay && World.WorldType != EWorldType::Editor)))) continue;
			if (!Args.Str(TEXT("WorldName")).IsEmpty() && World.World()->GetName() != Args.Str(TEXT("WorldName"))) continue;
			if (Context.World) { Error = TEXT("WorldName is ambiguous; specify an exact world name"); return false; }
			Context.World = World.World();
		}
	}
	if (!Context.World) { Error = TEXT("No world of the requested type/name is available"); return false; }
	if (!Args.Str(TEXT("ActorName")).IsEmpty())
	{
		for (TActorIterator<AActor> Actor(Context.World); Actor; ++Actor)
		{
			if (Actor->GetName() != Args.Str(TEXT("ActorName")) && FNexusRuntimeUtils::GetActorLabelOrName(*Actor) != Args.Str(TEXT("ActorName"))) continue;
			if (Context.Actor) { Error = TEXT("ActorName is ambiguous; use an exact actor name"); return false; }
			Context.Actor = *Actor;
		}
		if (!Context.Actor) { Error = TEXT("ActorName was not found in the selected world"); return false; }
	}
	if (Arguments->HasField(TEXT("ProjectionExtent")) && (!ReadPoint(Arguments, TEXT("ProjectionExtent"), Context.Extent) || Context.Extent.X < 0 || Context.Extent.Y < 0 || Context.Extent.Z < 0))
	{ Error = TEXT("ProjectionExtent must contain finite non-negative X/Y/Z values"); return false; }
	UClass* FilterClass = nullptr;
	if (!Args.Str(TEXT("FilterClass")).IsEmpty())
	{
		FilterClass = FNexusAssetUtils::FindClassWithUPrefix(Args.Str(TEXT("FilterClass")));
		if (!FilterClass || !FilterClass->IsChildOf(UNavigationQueryFilter::StaticClass())) { Error = TEXT("FilterClass must derive from NavigationQueryFilter"); return false; }
	}
	Context.System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context.World);
	Context.Data = Context.System ? Context.System->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	if (Context.Data) Context.Agent = Context.Data->GetConfig();
	const INavAgentInterface* Agent = Context.Actor ? Cast<INavAgentInterface>(Context.Actor) : nullptr;
	if (Agent) Context.Agent = Agent->GetNavAgentPropertiesRef();
	for (const TCHAR* Field : {TEXT("AgentRadius"), TEXT("AgentHeight")})
	{
		if (!Arguments->HasField(Field)) continue;
		const double Number = Args.Num(Field);
		if (!FMath::IsFinite(Number) || Number <= 0 || Number > MAX_flt) { Error = TEXT("AgentRadius/AgentHeight must be finite positive values"); return false; }
		if (FString(Field) == TEXT("AgentRadius")) Context.Agent.AgentRadius = static_cast<float>(Number);
		else Context.Agent.AgentHeight = static_cast<float>(Number);
	}
	if (Context.System && (Agent || Arguments->HasField(TEXT("AgentRadius")) || Arguments->HasField(TEXT("AgentHeight")))) Context.Data = Context.System->GetNavDataForProps(Context.Agent, Start);
	if (Context.Data)
	{
		Context.Filter = UNavigationQueryFilter::GetQueryFilter(*Context.Data, Context.Actor ? static_cast<UObject*>(Context.Actor) : static_cast<UObject*>(Context.World), FilterClass);
		Context.FilterClass = FilterClass ? FilterClass->GetPathName() : TEXT("Default");
	}
	return true;
}

void FNexusNavigationUtils::Describe(const FNexusNavigationContext& Context, const TSharedPtr<FJsonObject>& Result)
{
	Result->SetStringField(TEXT("WorldName"), Context.World->GetName());
	Result->SetStringField(TEXT("WorldType"), Context.World->WorldType == EWorldType::Editor ? TEXT("Editor") : (Context.World->WorldType == EWorldType::PIE ? TEXT("PIE") : TEXT("Game")));
	Result->SetBoolField(TEXT("bHasNavigationSystem"), Context.System != nullptr);
	Result->SetBoolField(TEXT("bNavigationAvailable"), Context.Data != nullptr);
	if (Context.Actor) Result->SetStringField(TEXT("ActorName"), Context.Actor->GetName());
	if (Context.System) Result->SetBoolField(TEXT("bNavigationBuilding"), Context.System->IsNavigationBuildInProgress());
	if (Context.Data)
	{
		Result->SetStringField(TEXT("NavData"), Context.Data->GetPathName());
		Result->SetStringField(TEXT("FilterClass"), Context.FilterClass);
		Result->SetNumberField(TEXT("AgentRadius"), Context.Agent.AgentRadius);
		Result->SetNumberField(TEXT("AgentHeight"), Context.Agent.AgentHeight);
	}
	else Result->SetStringField(TEXT("Hint"), TEXT("No existing navigation data matches this world/agent; queries never create or rebuild navigation"));
}
