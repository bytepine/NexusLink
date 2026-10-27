// Copyright byteyang. All Rights Reserved.
#include "NexusEditorOperation.h"
#include "Containers/Ticker.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Utils/NexusVersionCompat.h"

static TArray<TSharedPtr<FNexusEditorOperation>> GNexusOperations;
static TSet<FString> GNexusWaits;
static FString GNexusOperationOwner;
#if NX_UE_HAS_FTSTICKER_HANDLE
static TArray<FTSTicker::FDelegateHandle> GNexusOperationWaitHandles;
#else
static TArray<FDelegateHandle> GNexusOperationWaitHandles;
#endif

FString& FNexusEditorOperations::RequestOwner() { return GNexusOperationOwner; }

TSharedPtr<FJsonObject> FNexusEditorOperation::Snapshot() const
{
	auto Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("OperationId"), Id);
	Out->SetStringField(TEXT("Backend"), Backend);
	Out->SetStringField(TEXT("State"), State);
	Out->SetBoolField(TEXT("bCanCancel"), IsRunning() && !!Stop);
	Out->SetNumberField(TEXT("TtlSeconds"), 0);
	Out->SetStringField(TEXT("Message"), Message);
	Out->SetNumberField(TEXT("ElapsedSeconds"), FPlatformTime::Seconds() - StartedAt);
	if (Result.IsValid()) {
        FString Text; auto Writer = TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        TSharedPtr<FJsonObject> Copy; auto Reader = TJsonReaderFactory<>::Create(Text);
        if (FJsonSerializer::Deserialize(Reader, Copy)) Out->SetObjectField(TEXT("Result"), Copy);
    }
	return Out;
}

TSharedPtr<FNexusEditorOperation> FNexusEditorOperations::Create(const FString& Backend)
{
	check(IsInGameThread());
	// 有界保存最近结果；不能驱逐运行中的操作。
	while (GNexusOperations.Num() >= 128)
	{
		const int32 Index = GNexusOperations.IndexOfByPredicate([](const TSharedPtr<FNexusEditorOperation>& Item) { return !Item->IsRunning(); });
		if (Index == INDEX_NONE) return nullptr;
		GNexusOperations.RemoveAt(Index);
	}
	auto Operation = MakeShared<FNexusEditorOperation>();
	Operation->Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Operation->Owner = RequestOwner();
	Operation->Backend = Backend;
	Operation->StartedAt = FPlatformTime::Seconds();
	GNexusOperations.Add(Operation);
	return Operation;
}

TSharedPtr<FNexusEditorOperation> FNexusEditorOperations::Find(const FString& Id)
{
	check(IsInGameThread());
	for (const auto& Item : GNexusOperations)
		if (Item->Id == Id && Item->Owner == RequestOwner()) return Item;
	return nullptr;
}

void FNexusEditorOperations::Finish(const TSharedPtr<FNexusEditorOperation>& Operation,
	const FString& State, const TSharedPtr<FJsonObject>& Result, const FString& Message)
{
	if (!Operation.IsValid() || !Operation->IsRunning()) return;
	Operation->State = State;
	Operation->Result = Result;
	Operation->Message = Message;
	Operation->Stop = nullptr;
}

bool FNexusEditorOperations::Cancel(const FString& Id)
{
	auto Operation = Find(Id);
	if (!Operation.IsValid() || !Operation->IsRunning()) return false;
    const auto Stop = Operation->Stop; // Completion may clear the operation's stored callback.
    return Stop && Stop();
}

void FNexusEditorOperations::Wait(const FString& Id, double Seconds, const FString& RequestKey,
	TFunction<void(TSharedPtr<FJsonObject>)> Complete, TFunction<void(const FString&)> Progress)
{
	auto Operation = Find(Id);
	if (!Operation.IsValid()) { Complete(nullptr); return; }
	if (!Operation->IsRunning() || Seconds <= 0) { Complete(Operation->Snapshot()); return; }
	if (GNexusWaits.Num() >= 512 || GNexusWaits.Contains(RequestKey)) { Complete(nullptr); return; }
	GNexusWaits.Add(RequestKey);
	const double Deadline = FPlatformTime::Seconds() + FMath::Clamp(Seconds, 0.0, 30.0);
	auto Tick = FTickerDelegate::CreateLambda([Operation, RequestKey, Deadline, Complete, Progress](float) mutable
	{
		const bool bCancelled = !GNexusWaits.Contains(RequestKey);
		if (bCancelled || !Operation->IsRunning() || FPlatformTime::Seconds() >= Deadline)
		{
			GNexusWaits.Remove(RequestKey);
			auto Snapshot = Operation->Snapshot();
			if (bCancelled) Snapshot->SetBoolField(TEXT("bWaitCancelled"), true);
			Complete(Snapshot);
			return false;
		}
		if (Progress) Progress(Operation->Message);
		return true;
	});
#if NX_UE_HAS_FTSTICKER_HANDLE
	GNexusOperationWaitHandles.Add(FTSTicker::GetCoreTicker().AddTicker(Tick, 1.0f));
#else
	GNexusOperationWaitHandles.Add(FTicker::GetCoreTicker().AddTicker(Tick, 1.0f));
#endif
}

void FNexusEditorOperations::CancelWait(const FString& RequestKey) { GNexusWaits.Remove(RequestKey); }
void FNexusEditorOperations::ShutdownOwner(const FString& Owner)
{
	for (auto It = GNexusWaits.CreateIterator(); It; ++It) if (It->StartsWith(Owner + TEXT("|"))) It.RemoveCurrent();
}
void FNexusEditorOperations::Reset()
{
    for (auto Handle : GNexusOperationWaitHandles) {
#if NX_UE_HAS_FTSTICKER_HANDLE
        FTSTicker::GetCoreTicker().RemoveTicker(Handle);
#else
        FTicker::GetCoreTicker().RemoveTicker(Handle);
#endif
    }
    GNexusOperationWaitHandles.Empty();
	GNexusWaits.Empty();
	for (const auto& Item : GNexusOperations) Finish(Item, TEXT("Interrupted"), nullptr, TEXT("Editor services stopped; underlying work may still be active."));
	GNexusOperations.Empty();
}
