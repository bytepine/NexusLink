// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusVersionCompat.h"
#include "IAutomationControllerModule.h"
#include "IAutomationControllerManager.h"
#include "IAutomationReport.h"
#include "ISessionServicesModule.h"
#include "ISessionManager.h"
#include "ISessionInfo.h"
#include "Modules/ModuleManager.h"
#include "Misc/App.h"
#include "Misc/FilterCollection.h"
#include "Dom/JsonValue.h"
#include "Containers/Ticker.h"

static IAutomationControllerManagerPtr GNexusAutomation;
static TSharedPtr<FNexusEditorOperation> GNexusAutomationActive;
static TArray<TSharedPtr<IAutomationReport>> GNexusAutomationTests;
static TArray<FString> GNexusAutomationSelection;
static bool bNexusAutomationReady = false;
static bool bNexusAutomationRefreshed = false;
static bool bNexusAutomationSawRunning = false;
static FDelegateHandle GNexusAutomationRefreshHandle;
static uint32 GNexusAutomationGeneration = 0;

static bool NexusOwnsAutomationSelection()
{
    auto Manager = FModuleManager::LoadModuleChecked<ISessionServicesModule>(TEXT("SessionServices")).GetSessionManager();
    if (Manager.IsValid() && Manager->GetSelectedSession().IsValid() && Manager->GetSelectedSession()->GetSessionId() != FApp::GetSessionId()) return false;
    TArray<FString> Enabled; GNexusAutomation->GetEnabledTestNames(Enabled);
    if (Enabled.Num() != GNexusAutomationSelection.Num()) return false;
    for (const auto& Name : Enabled) if (!GNexusAutomationSelection.Contains(Name)) return false;
    return true;
}

static void NexusCollectAutomation(const TArray<TSharedPtr<IAutomationReport>>& Reports, TArray<TSharedPtr<IAutomationReport>>& Leaves)
{
	for (const auto& Report : Reports)
	{
		if (!Report.IsValid()) continue;
		if (!Report->GetChildReports().Num()) Leaves.Add(Report);
		else NexusCollectAutomation(Report->GetChildReports(), Leaves);
	}
}

static TSharedPtr<FJsonObject> NexusAutomationResults(const TSet<FString>* Evidence = nullptr)
{
	auto Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Tests;
	for (const auto& Report : GNexusAutomationTests)
	{
		if (!GNexusAutomationSelection.Contains(Report->GetFullTestPath())) continue;
		auto Test = MakeShared<FJsonObject>(); Test->SetStringField(TEXT("Name"), Report->GetFullTestPath());
		const auto& Details = Report->GetResults(0, 0);
		const auto State = Report->GetState(0, 0);
        const bool bCurrent = !Evidence || Evidence->Contains(Report->GetFullTestPath());
        Test->SetStringField(TEXT("Outcome"), !bCurrent ? TEXT("Unknown") : State == EAutomationState::Success ? TEXT("Passed") : State == EAutomationState::Fail ? TEXT("Failed") : TEXT("Unknown"));
		Test->SetNumberField(TEXT("DurationSeconds"), bCurrent ? Details.Duration : 0);
        TArray<TSharedPtr<FJsonValue>> Errors, Warnings;
        if (bCurrent) for (const auto& Event : Details.GetEntries()) {
            if (Event.Event.Type == EAutomationEventType::Error) Errors.Add(MakeShared<FJsonValueString>(Event.Event.Message));
            else if (Event.Event.Type == EAutomationEventType::Warning) Warnings.Add(MakeShared<FJsonValueString>(Event.Event.Message));
        }
        Test->SetArrayField(TEXT("Errors"), Errors); Test->SetArrayField(TEXT("Warnings"), Warnings);
		Tests.Add(MakeShared<FJsonValueObject>(Test));
	}
	Result->SetArrayField(TEXT("Tests"), Tests); return Result;
}

TSharedPtr<FNexusEditorOperation> FNexusEditorToolsets::Automation(const FString& Action, const TSharedPtr<FJsonObject>& Args)
{
	const bool bNative = FNexusNativeEditorTools::UseNative(TEXT("AutomationTestToolset"));
	auto Operation = FNexusEditorOperations::Create(bNative ? TEXT("OfficialToolset") : TEXT("EngineAutomation"));
	if (!Operation.IsValid()) return nullptr;
	auto& ControllerModule = FModuleManager::LoadModuleChecked<IAutomationControllerModule>(TEXT("AutomationController"));
	if (!GNexusAutomation.IsValid())
	{
		GNexusAutomation = ControllerModule.GetAutomationController();
		GNexusAutomationRefreshHandle = GNexusAutomation->OnTestsRefreshed().AddLambda([]() { bNexusAutomationRefreshed = true; });
	}
	auto Fail = [Operation](const FString& Message) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Message); return Operation; };
	if (Action == TEXT("Stop"))
	{
		FString Id; Args->TryGetStringField(TEXT("OperationId"), Id);
		if (!GNexusAutomationActive.IsValid() || FNexusEditorOperations::Find(Id) != GNexusAutomationActive || !FNexusEditorOperations::Cancel(Id))
			return Fail(TEXT("No cancellable operation owned by this session"));
        const auto Target = GNexusAutomationActive;
        auto CompleteStop = [Operation, Target]() {
            auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("bApplied"), Target->State == TEXT("Cancelled"));
            FNexusEditorOperations::Finish(Operation, Target->State == TEXT("Failed") ? TEXT("Failed") : TEXT("Completed"), Result,
                Target->State == TEXT("Cancelled") ? TEXT("Stopped this adapter run") : TEXT("Run ended before stop confirmation; inspect the target operation"));
        };
        if (!Target->IsRunning()) CompleteStop();
        else FNexusEditorToolsets::AddTicker(FTickerDelegate::CreateLambda([Operation, Target, CompleteStop](float) {
            if (!Target->IsRunning()) { CompleteStop(); return false; }
            if (FPlatformTime::Seconds() - Operation->StartedAt > 5) {
                FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Stop was not confirmed; target remains queryable and may still be running")); return false;
            }
            return true;
        }), 0.05f);
        return Operation;
	}
	if (GNexusAutomationActive.IsValid() && GNexusAutomationActive->IsRunning())
		return Fail(TEXT("Busy: an adapter operation is active"));
	if (GNexusAutomation->GetTestState() == EAutomationControllerModuleState::Running)
		return Fail(TEXT("Busy: automation is already running outside this adapter"));
	// 官方 Subsystem 会使用所选会话。拒绝外部会话，不能悄悄切换 Session Frontend。
	auto SessionManager = FModuleManager::LoadModuleChecked<ISessionServicesModule>(TEXT("SessionServices")).GetSessionManager();
	if (SessionManager.IsValid() && SessionManager->GetSelectedSession().IsValid()
		&& SessionManager->GetSelectedSession()->GetSessionId() != FApp::GetSessionId())
		return Fail(TEXT("A foreign automation session is selected; select the local editor session first"));
	if (Action == TEXT("List"))
	{
		if (!bNexusAutomationReady) return Fail(TEXT("Discover tests first and wait for completion"));
		if (bNative)
		{
			auto Input = MakeShared<FJsonObject>();
			FString Filter; Args->TryGetStringField(TEXT("NameFilter"), Filter); Input->SetStringField(TEXT("NameFilter"), Filter);
			double Limit = 200; Args->TryGetNumberField(TEXT("Limit"), Limit); Limit = FMath::Clamp(Limit, 1.0, 2000.0); Input->SetNumberField(TEXT("Limit"), Limit);
			FNexusNativeEditorTools::Call(TEXT("AutomationTestToolset"), TEXT("ListTests"), Input, Operation);
		}
		else
		{
			FString Filter; Args->TryGetStringField(TEXT("NameFilter"), Filter);
			double Limit = 200; Args->TryGetNumberField(TEXT("Limit"), Limit); Limit = FMath::Clamp(Limit, 1.0, 2000.0);
			TArray<TSharedPtr<FJsonValue>> Tests; int32 Total = 0;
			for (const auto& Test : GNexusAutomationTests)
				if (Filter.IsEmpty() || Test->GetFullTestPath().Contains(Filter)) { ++Total; if (Tests.Num() < Limit) Tests.Add(MakeShared<FJsonValueString>(Test->GetFullTestPath())); }
			auto Result = MakeShared<FJsonObject>(); Result->SetArrayField(TEXT("Tests"), Tests); Result->SetNumberField(TEXT("Total"), Total);
			FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
		}
		return Operation;
	}
	if (Action != TEXT("Discover") && Action != TEXT("Run")) return Fail(TEXT("Unknown automation action"));
	if (Action == TEXT("Run") && !bNexusAutomationReady) return Fail(TEXT("Discover tests first"));
	GNexusAutomationSelection.Empty();
	if (Action == TEXT("Run"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (!Args->TryGetArrayField(TEXT("TestNames"), Names) || !Names || !Names->Num()) return Fail(TEXT("TestNames must contain full test names"));
		for (const auto& Name : *Names)
		{
			const FString Path = Name->AsString();
			if (!GNexusAutomationTests.ContainsByPredicate([&Path](const TSharedPtr<IAutomationReport>& Test) { return Test->GetFullTestPath() == Path; }))
				return Fail(TEXT("Unknown full test name: ") + Path);
			GNexusAutomationSelection.AddUnique(Path);
		}
	}
    auto NativeOperation = bNative ? FNexusNativeEditorTools::Scratch() : Operation;
    auto StopPending = MakeShared<bool>(false);
    auto ResetEvidence = MakeShared<TSet<FString>>();
	GNexusAutomationActive = Operation;
	Operation->Message = Action == TEXT("Discover") ? TEXT("Discovering local editor tests") : TEXT("Running selected tests");
	if (Action == TEXT("Run")) Operation->Stop = [Operation, bNative, StopPending]()
	{
		if (GNexusAutomationActive != Operation || !Operation->IsRunning() || *StopPending || !NexusOwnsAutomationSelection()) return false;
		if (bNative)
		{
            if (!FNexusNativeEditorTools::CanCall(TEXT("AutomationTestToolset"), TEXT("StopTests"))) return false;
			auto Stop = FNexusNativeEditorTools::Scratch();
            *StopPending = true;
            FNexusNativeEditorTools::Call(TEXT("AutomationTestToolset"), TEXT("StopTests"), MakeShared<FJsonObject>(), Stop);
            FNexusEditorToolsets::AddTicker(FTickerDelegate::CreateLambda([Stop, Operation, StopPending](float) {
                if (Stop->IsRunning()) return true;
                if (Stop->State == TEXT("Completed")) FNexusEditorOperations::Finish(Operation, TEXT("Cancelled"), NexusAutomationResults(), TEXT("Stopped this adapter run"));
                else { *StopPending = false; Operation->Message = TEXT("Stop failed: ") + Stop->Message; }
                return false;
            }), 0.05f);
        }
        else { GNexusAutomation->StopTests(); FNexusEditorOperations::Finish(Operation, TEXT("Cancelled"), NexusAutomationResults(), TEXT("Stopped this adapter run")); }
        return true;
	};
	if (bNative)
	{
		auto Input = MakeShared<FJsonObject>();
		if (Action == TEXT("Run")) Input->SetArrayField(TEXT("TestNames"), Args->GetArrayField(TEXT("TestNames")));
		FNexusNativeEditorTools::Call(TEXT("AutomationTestToolset"), Action == TEXT("Discover") ? TEXT("DiscoverTests") : TEXT("RunTests"), Input, NativeOperation);
	}
	else if (Action == TEXT("Discover"))
	{
		bNexusAutomationReady = false; bNexusAutomationRefreshed = false;
		GNexusAutomation->RequestAvailableWorkers(FApp::GetSessionId());
	}
	else
	{
		GNexusAutomation->SetEnabledTests(GNexusAutomationSelection);
		bNexusAutomationSawRunning = false;
		GNexusAutomation->RunTests(true);
	}
    // A terminal cached report can belong to a previous run. Record the controller reset
    // before accepting its result as supplemental evidence for a native response.
    for (const auto& Test : GNexusAutomationTests) if (GNexusAutomationSelection.Contains(Test->GetFullTestPath())) {
        const auto State = Test->GetState(0, 0);
        if (State != EAutomationState::Success && State != EAutomationState::Fail) ResetEvidence->Add(Test->GetFullTestPath());
    }
	const uint32 Generation = GNexusAutomationGeneration;
	auto Tick = FTickerDelegate::CreateLambda([Operation, NativeOperation, StopPending, ResetEvidence, bNative, Action, Generation](float)
	{
		if (Generation != GNexusAutomationGeneration) return false;
        if (Action == TEXT("Run") && Operation->IsRunning() && (!bNative || NativeOperation->IsRunning())
            && GNexusAutomation->GetTestState() == EAutomationControllerModuleState::Running && !NexusOwnsAutomationSelection()) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Automation session or test selection changed outside the adapter; result ownership lost, no stop attempted")); return false;
        }
        if (!bNative) {
            IAutomationControllerModule::Get().Tick(); GNexusAutomation->Tick();
        }
		if (Action == TEXT("Discover"))
		{
			if ((bNative && !NativeOperation->IsRunning() && NativeOperation->State == TEXT("Completed")) || (!bNative && bNexusAutomationRefreshed))
			{
                if (!bNative) {
                    GNexusAutomation->SetFilter(MakeShared<AutomationFilterCollection>());
                    GNexusAutomation->SetVisibleTestsEnabled(true);
                }
				GNexusAutomationTests.Empty();
#if NX_UE_HAS_AUTOMATION_FILTERED_REPORTS
                NexusCollectAutomation(GNexusAutomation->GetFilteredReports(), GNexusAutomationTests);
#else
                NexusCollectAutomation(GNexusAutomation->GetReports(), GNexusAutomationTests);
#endif
				bNexusAutomationReady = true;
				auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("Total"), GNexusAutomationTests.Num());
				FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
				return false;
			}
            if (bNative && !NativeOperation->IsRunning() && NativeOperation->State != TEXT("Completed"))
                FNexusEditorOperations::Finish(Operation, NativeOperation->State, nullptr, NativeOperation->Message);
			if (Operation->IsRunning() && FPlatformTime::Seconds() - Operation->StartedAt > 60)
				FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Local test discovery timed out"));
		}
        else if (bNative && !*StopPending && !NativeOperation->IsRunning()) {
            auto Result = NativeOperation->Result;
            const TArray<TSharedPtr<FJsonValue>>* Tests = nullptr;
            if (NativeOperation->State == TEXT("Completed") && (!Result.IsValid() || !Result->TryGetArrayField(TEXT("Tests"), Tests) || Tests->Num() != GNexusAutomationSelection.Num())) {
                // Official filtered reports can omit a selected leaf (e.g. a test under a
                // runnable parent). Read this run's retained controller records; never run again.
                Result = NexusAutomationResults(&ResetEvidence.Get());
                Operation->Message = TEXT("Official response omitted selected tests; using this run's controller records, unknown where evidence is absent");
            }
            FNexusEditorOperations::Finish(Operation, NativeOperation->State, Result, Operation->Message);
        }
		else if (!bNative && Operation->IsRunning())
		{
			if (GNexusAutomation->GetTestState() == EAutomationControllerModuleState::Running) bNexusAutomationSawRunning = true;
            else {
                bool bAllTerminal = GNexusAutomationSelection.Num() > 0;
                for (const auto& Test : GNexusAutomationTests) if (GNexusAutomationSelection.Contains(Test->GetFullTestPath())) {
                    const auto State = Test->GetState(0, 0);
                    if (State != EAutomationState::Success && State != EAutomationState::Fail) bAllTerminal = false;
                }
                if (bNexusAutomationSawRunning || bAllTerminal) FNexusEditorOperations::Finish(Operation, TEXT("Completed"), NexusAutomationResults());
            }
		}
		return Operation->IsRunning();
	});
FNexusEditorToolsets::AddTicker(Tick, 0.1f);
	return Operation;
}

static void NexusRemoveEditorToolTicks();

void FNexusEditorToolsets::Shutdown()
{
	NexusRemoveEditorToolTicks();
	ShutdownLiveCoding();
	++GNexusAutomationGeneration;
	if (GNexusAutomation.IsValid()) GNexusAutomation->OnTestsRefreshed().Remove(GNexusAutomationRefreshHandle);
	GNexusAutomation.Reset(); GNexusAutomationActive.Reset(); GNexusAutomationTests.Empty(); bNexusAutomationReady = false;
	FNexusNativeEditorTools::Shutdown(); FNexusEditorOperations::Reset();
}

#if NX_UE_HAS_FTSTICKER
static TArray<FTSTicker::FDelegateHandle> GNexusEditorToolTicks;
#else
static TArray<FDelegateHandle> GNexusEditorToolTicks;
#endif
void FNexusEditorToolsets::AddTicker(const FTickerDelegate& Delegate, float Interval)
{
#if NX_UE_HAS_FTSTICKER
 GNexusEditorToolTicks.Add(FTSTicker::GetCoreTicker().AddTicker(Delegate, Interval));
#else
 GNexusEditorToolTicks.Add(FTicker::GetCoreTicker().AddTicker(Delegate, Interval));
#endif
}

static void NexusRemoveEditorToolTicks()
{
 for (auto Handle : GNexusEditorToolTicks) {
#if NX_UE_HAS_FTSTICKER
  FTSTicker::GetCoreTicker().RemoveTicker(Handle);
#else
  FTicker::GetCoreTicker().RemoveTicker(Handle);
#endif
 }
 GNexusEditorToolTicks.Empty();
}
