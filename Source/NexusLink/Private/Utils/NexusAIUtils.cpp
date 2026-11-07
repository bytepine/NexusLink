// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAIUtils.h"
#include "Utils/NexusNavigationUtils.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"

struct FNexusMoveRecord
{
	FDelegateHandle Handle;
	TWeakObjectPtr<UPathFollowingComponent> Component;
	FAIRequestID TrackedId;
	FAIRequestID FinishedId;
	EPathFollowingResult::Type Result = EPathFollowingResult::Invalid;
	int32 Flags = 0;
	bool bFinished = false;
	bool bRejected = false;
	bool bAlreadyAtGoal = false;
};
static TMap<TWeakObjectPtr<AAIController>, FNexusMoveRecord> NexusMoves;

AAIController* FNexusAIUtils::ResolveController(AActor* Actor)
{
	if (AAIController* Controller = Cast<AAIController>(Actor)) return Controller;
	APawn* Pawn = Cast<APawn>(Actor);
	return Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
}

void FNexusAIUtils::BeginMove(AAIController* Controller)
{
	for (auto It = NexusMoves.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	FNexusMoveRecord& Record = NexusMoves.FindOrAdd(Controller);
	Record.bFinished = Record.bRejected = Record.bAlreadyAtGoal = false;
	Record.TrackedId = Record.FinishedId = FAIRequestID::InvalidRequest;
	UPathFollowingComponent* Component = Controller->GetPathFollowingComponent();
	if (Record.Component.Get() == Component && Record.Handle.IsValid()) return;
	if (Record.Component.IsValid()) Record.Component->OnRequestFinished.Remove(Record.Handle);
	Record.Component = Component;
	if (Component)
	{
		const TWeakObjectPtr<AAIController> WeakController(Controller);
		Record.Handle = Component->OnRequestFinished.AddLambda([WeakController](FAIRequestID Id, const FPathFollowingResult& Result)
		{
			FNexusMoveRecord* Tracked = NexusMoves.Find(WeakController);
			if (!Tracked) return;
			if (Tracked->TrackedId.IsValid() && !Tracked->TrackedId.IsEquivalent(Id)) return;
			Tracked->FinishedId = Id;
			Tracked->Result = Result.Code;
			Tracked->Flags = static_cast<int32>(Result.Flags);
			Tracked->bFinished = true;
		});
	}
}

void FNexusAIUtils::Shutdown()
{
	for (const auto& Pair : NexusMoves) if (Pair.Value.Component.IsValid()) Pair.Value.Component->OnRequestFinished.Remove(Pair.Value.Handle);
	NexusMoves.Reset();
}

void FNexusAIUtils::AcceptMove(AAIController* Controller, int32 Result)
{
	FNexusMoveRecord* Record = NexusMoves.Find(Controller);
	if (!Record) return;
	Record->bRejected = Result == EPathFollowingRequestResult::Failed;
	Record->bAlreadyAtGoal = Result == EPathFollowingRequestResult::AlreadyAtGoal;
	Record->TrackedId = Record->bAlreadyAtGoal ? Record->FinishedId : Controller->GetCurrentMoveRequestID();
}

void FNexusAIUtils::Describe(AAIController* Controller, const TSharedPtr<FJsonObject>& Result)
{
	Result->SetStringField(TEXT("ControllerName"), Controller->GetName());
	const EPathFollowingStatus::Type Status = Controller->GetMoveStatus();
	Result->SetStringField(TEXT("MoveStatus"), Status == EPathFollowingStatus::Moving ? TEXT("Moving") : (Status == EPathFollowingStatus::Paused ? TEXT("Paused") : (Status == EPathFollowingStatus::Waiting ? TEXT("Waiting") : TEXT("Idle"))));
	Result->SetNumberField(TEXT("CurrentRequestId"), static_cast<uint32>(Controller->GetCurrentMoveRequestID()));
	if (Status != EPathFollowingStatus::Idle) Result->SetObjectField(TEXT("ImmediateDestination"), FNexusNavigationUtils::PointJson(Controller->GetImmediateMoveDestination()));
	const FNexusMoveRecord* Record = NexusMoves.Find(Controller);
	Result->SetBoolField(TEXT("bHasTrackedRequest"), Record != nullptr);
	if (!Record) { Result->SetStringField(TEXT("Outcome"), TEXT("Unknown")); return; }
	Result->SetNumberField(TEXT("TrackedRequestId"), static_cast<uint32>(Record->TrackedId));
	const bool bCompleted = Record->bAlreadyAtGoal || (Record->bFinished && Record->FinishedId.IsEquivalent(Record->TrackedId));
	Result->SetBoolField(TEXT("bCompleted"), bCompleted);
	const TCHAR* Outcome = TEXT("Pending");
	if (Record->bRejected) Outcome = TEXT("Rejected");
	else if (Record->bAlreadyAtGoal) Outcome = TEXT("AlreadyAtGoal");
	else if (bCompleted)
	{
		Outcome = Record->Result == EPathFollowingResult::Success ? TEXT("Success") : (Record->Result == EPathFollowingResult::Blocked ? TEXT("Blocked") : (Record->Result == EPathFollowingResult::OffPath ? TEXT("OffPath") : (Record->Result == EPathFollowingResult::Aborted ? TEXT("Aborted") : TEXT("Invalid"))));
		Result->SetNumberField(TEXT("ResultFlags"), Record->Flags);
	}
	Result->SetStringField(TEXT("Outcome"), Outcome);
}
