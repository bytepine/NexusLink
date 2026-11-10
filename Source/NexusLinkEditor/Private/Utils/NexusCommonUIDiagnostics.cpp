// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusCommonUIDiagnostics.h"
#if WITH_NEXUS_COMMON_UI_DEBUGGING
#include "Utils/NexusArgs.h"
#include "NexusEditorOperation.h"
#include "CommonUIQuery.h"
#include "CommonUIEventLogger.h"
#include "CommonUIDebuggingTypes.h"
#include "JsonObjectConverter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"

static TSharedPtr<FNexusEditorOperation> GNexusCommonUICapture;
static int64 GNexusCommonUIEpoch = -1;
static FTSTicker::FDelegateHandle GNexusCommonUITimer;

template<typename T>
static TSharedRef<FJsonObject> NexusCommonUIJson(const T& Value)
{
	auto Json = MakeShared<FJsonObject>();
	FJsonObjectConverter::UStructToJsonObject(T::StaticStruct(), &Value, Json, 0, 0, nullptr, EJsonObjectConversionFlags::SkipStandardizeCase);
	return Json;
}
static void NexusEndCommonUICapture(const FString& State)
{
	if (!GNexusCommonUICapture || !GNexusCommonUICapture->IsRunning()) return;
	auto& Logger = FCommonUIEventLogger::Get();
	const bool bStillOwned = Logger.GetEpoch() == GNexusCommonUIEpoch;
	if (bStillOwned) Logger.Stop();
	auto Result = NexusCommonUIJson(Logger.GetStatus());
	Result->SetNumberField(TEXT("Epoch"), Logger.GetEpoch());
	FNexusEditorOperations::Finish(GNexusCommonUICapture, bStillOwned ? State : TEXT("Interrupted"), Result, bStillOwned ? TEXT("Event logging stopped; retained events remain readable") : TEXT("Event logger was replaced by another consumer"));
}
void FNexusCommonUIDiagnostics::Read(const FString& Section, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Result, FString& Error)
{
	const FNexusArgs Arguments(Args);
	const int32 PlayerIndex = Arguments.Int(TEXT("LocalPlayerIndex"), 0), Offset = Arguments.Int(TEXT("Offset"), 0), Limit = Arguments.Int(TEXT("Limit"), 100);
	if (PlayerIndex < -1 || Offset < 0 || Offset > 1000000 || Limit < 1 || Limit > 500) { Error = TEXT("LocalPlayerIndex >= -1, Offset 0-1000000 and Limit 1-500 required"); return; }
	Result->SetNumberField(TEXT("TtlSeconds"), 0);
	if (Section == TEXT("Players"))
	{
		TArray<ULocalPlayer*> Players; CommonUIQuery::GatherSessionLocalPlayers(Players);
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 Index = Offset; Index < FMath::Min(Players.Num(), Offset + Limit); ++Index)
		{
			if (!Players[Index]) continue;
			auto Item = MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("LocalPlayerIndex"), Index);
			Item->SetStringField(TEXT("PlayerId"), CommonUIQuery::MakePlayerId(Players[Index]).ToString());
			if (Players[Index]->GetWorld()) Item->SetStringField(TEXT("WorldName"), Players[Index]->GetWorld()->GetName());
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Result->SetArrayField(TEXT("Players"), Items); Result->SetNumberField(TEXT("PlayerCount"), Players.Num());
		return;
	}
	if (Section == TEXT("EventLog"))
	{
		auto& Logger = FCommonUIEventLogger::Get();
		const double Epoch = Arguments.Num(TEXT("Epoch"), -1), AfterSeq = Arguments.Num(TEXT("AfterSeq"), -1);
		if (!FMath::IsFinite(Epoch) || !FMath::IsFinite(AfterSeq) || Epoch < -1 || AfterSeq < -1 || Epoch > 9007199254740991.0 || AfterSeq > 9007199254740991.0 || FMath::FloorToDouble(Epoch) != Epoch || FMath::FloorToDouble(AfterSeq) != AfterSeq)
		{ Error = TEXT("Epoch/AfterSeq must be -1 or exact nonnegative integers"); return; }
		if (Epoch >= 0 && static_cast<int64>(Epoch) != Logger.GetEpoch()) { Error = TEXT("Event log epoch changed: discard previous cursor and read the new epoch"); return; }
		FCommonUIPlayerId PlayerId;
		if (PlayerIndex >= 0) { PlayerId = CommonUIQuery::ResolvePlayerIdForIndex(PlayerIndex); if (!PlayerId.IsValid()) { Error = TEXT("Local player not found; -1 reads retained events for all players"); return; } }
		TArray<FCommonUIEvent> Events; Logger.GetEventsOldestFirst(Events);
		TArray<TSharedPtr<FJsonValue>> Items; int32 MatchCount = 0;
		for (const auto& Event : Events)
		{
			if (Event.Seq <= AfterSeq || (PlayerId.IsValid() && Event.PlayerId != PlayerId)) continue;
			if (MatchCount++ < Offset || Items.Num() >= Limit) continue;
			auto Item = NexusCommonUIJson(Event);
			Item->SetStringField(TEXT("PlayerId"), Event.PlayerId.ToString());
			Item->RemoveField(TEXT("CallstackAddrs")); Item->RemoveField(TEXT("Callstack"));
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Result->SetArrayField(TEXT("Events"), Items); Result->SetNumberField(TEXT("EventMatchCount"), MatchCount);
		Result->SetObjectField(TEXT("EventLogStatus"), NexusCommonUIJson(Logger.GetStatus()));
		Result->SetNumberField(TEXT("Epoch"), Logger.GetEpoch());
		Result->SetBoolField(TEXT("bEventsTruncated"), Offset + Items.Num() < MatchCount);
		return;
	}
	if (PlayerIndex < 0) { Error = TEXT("Select a single LocalPlayerIndex from Players"); return; }
	if (Section == TEXT("ActivatableTree"))
	{
		auto Snapshot = CommonUIQuery::BuildTreeSnapshot(PlayerIndex, false, Error);
		if (!Error.IsEmpty()) return;
		auto Json = NexusCommonUIJson(Snapshot);
		int32 Truncated = 0;
		// 前序节点的父索引总在前方，截取前缀后保留原节点索引。
		for (const TCHAR* Field : {TEXT("TreeNodes"), TEXT("TreeRoots"), TEXT("RootNodes"), TEXT("ActionDomainRoots")})
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (Json->TryGetArrayField(Field, Array) && Array && Array->Num() > Limit)
			{
				TArray<TSharedPtr<FJsonValue>> Page; for (int32 Index = 0; Index < Limit; ++Index) Page.Add((*Array)[Index]);
				Truncated += Array->Num() - Limit; Json->SetArrayField(Field, Page);
			}
		}
		Json->RemoveField(TEXT("FullTreeDump")); Json->SetNumberField(TEXT("TruncatedRecords"), Truncated);
		Result->SetObjectField(TEXT("ActivatableTree"), Json);
	}
	else if (Section == TEXT("InputRouting"))
	{
		const auto State = CommonUIQuery::BuildInputRoutingState(PlayerIndex, Error);
		if (Error.IsEmpty()) Result->SetObjectField(TEXT("InputRouting"), NexusCommonUIJson(State));
	}
	else if (Section == TEXT("Focus"))
	{
		const auto State = CommonUIQuery::BuildFocusState(PlayerIndex, Error);
		if (Error.IsEmpty()) Result->SetObjectField(TEXT("Focus"), NexusCommonUIJson(State));
	}
	else if (Section == TEXT("ActionBindings"))
	{
		TArray<FCommonUIActionBindingInfo> Bindings;
		if (!CommonUIQuery::GatherActionBindings(PlayerIndex, Bindings, Error)) return;
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 Index = Offset; Index < FMath::Min(Bindings.Num(), Offset + Limit); ++Index) Items.Add(MakeShared<FJsonValueObject>(NexusCommonUIJson(Bindings[Index])));
		Result->SetArrayField(TEXT("ActionBindings"), Items); Result->SetNumberField(TEXT("BindingCount"), Bindings.Num());
	}
}
FCapabilityResult FNexusCommonUIDiagnostics::Control(const TSharedPtr<FJsonObject>& Args)
{
	const FNexusArgs Arguments(Args);
	if (Arguments.Str(TEXT("Action")) == TEXT("Stop"))
	{
		const auto Operation = FNexusEditorOperations::Find(Arguments.Str(TEXT("OperationId")));
		if (!Operation || Operation != GNexusCommonUICapture) return FCapabilityResult::MakeArgInvalid(TEXT("OperationId must identify this session's CommonUI capture"));
		if (Operation->IsRunning()) FNexusEditorOperations::Cancel(Operation->Id);
		FCapabilityResult Result; Result.Entries.Add(MakeShared<FJsonValueObject>(Operation->Snapshot())); return Result;
	}
	auto& Logger = FCommonUIEventLogger::Get();
	const double Duration = Arguments.Num(TEXT("DurationSeconds"), 10);
	const int32 Size = Arguments.Int(TEXT("BufferSize"), 512);
	if (!FMath::IsFinite(Duration) || Duration < 1 || Duration > 60 || Size < 64 || Size > 4096) return FCapabilityResult::MakeArgInvalid(TEXT("DurationSeconds 1-60 and BufferSize 64-4096 required"));
	if (Logger.IsLogging()) return FCapabilityResult::MakeFatal(TEXT("Event logger is already active; existing consumers are not replaced"));
	TArray<ULocalPlayer*> Players; CommonUIQuery::GatherSessionLocalPlayers(Players);
	if (!Players.Num()) return FCapabilityResult::MakeFatal(TEXT("CommonUI event capture requires a PIE local player"));
	auto Operation = FNexusEditorOperations::Create(TEXT("NativeCommonUIEvents"));
	if (!Operation) return FCapabilityResult::MakeFatal(TEXT("Operation capacity reached"));
	FTSTicker::GetCoreTicker().RemoveTicker(GNexusCommonUITimer);
	FCommonUIEventLoggerConfig Config; Config.BufferSize = Size; Config.bCaptureCallstacks = false;
	Logger.Start(Config); GNexusCommonUIEpoch = Logger.GetEpoch(); GNexusCommonUICapture = Operation;
	Operation->Stop = []() { NexusEndCommonUICapture(TEXT("Cancelled")); return true; };
	const double Deadline = FPlatformTime::Seconds() + Duration;
	GNexusCommonUITimer = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Deadline](float)
	{
		auto& ActiveLogger = FCommonUIEventLogger::Get();
		if (FPlatformTime::Seconds() >= Deadline || !ActiveLogger.IsLogging() || ActiveLogger.GetEpoch() != GNexusCommonUIEpoch)
		{ NexusEndCommonUICapture(TEXT("Completed")); return false; }
		return GNexusCommonUICapture && GNexusCommonUICapture->IsRunning();
	}), 0.1f);
	auto Snapshot = Operation->Snapshot(); Snapshot->SetNumberField(TEXT("Epoch"), GNexusCommonUIEpoch);
	FCapabilityResult Result; Result.Entries.Add(MakeShared<FJsonValueObject>(Snapshot)); return Result;
}
#endif

void FNexusCommonUIDiagnostics::Shutdown()
{
#if WITH_NEXUS_COMMON_UI_DEBUGGING
	NexusEndCommonUICapture(TEXT("Interrupted"));
	FTSTicker::GetCoreTicker().RemoveTicker(GNexusCommonUITimer); GNexusCommonUITimer.Reset();
	GNexusCommonUICapture.Reset();
#endif
}
