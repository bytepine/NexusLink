// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusRuntimeInputUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusEditorOperation.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Containers/Ticker.h"
#if NX_UE_HAS_INPUT_KEY_EVENT_ARGS
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#endif
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#endif

struct FNexusHeldInput final
{
	FKey Key;
	FVector Value = FVector::ZeroVector;
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
	TWeakObjectPtr<UInputAction> InputAction;
#endif
	FString Identity;
};
struct FNexusInputHold final
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<ULocalPlayer> Player;
	TArray<FNexusHeldInput> Inputs;
	TSharedPtr<FNexusEditorOperation> Operation;
	double Deadline = 0, LeaseDeadline = 0;
	bool bReleased = false;
};
static TArray<TSharedPtr<FNexusInputHold>> GNexusInputHolds;
static FDelegateHandle GNexusInputPreTick, GNexusInputPostTick, GNexusInputCleanup;
#if NX_UE_HAS_FTSTICKER_HANDLE
static FTSTicker::FDelegateHandle GNexusInputLeaseTicker;
#else
static FDelegateHandle GNexusInputLeaseTicker;
#endif

static void NexusInjectKey(APlayerController* Controller, const FKey& Key, EInputEvent Event, float Value)
{
	if (!Controller || !Controller->PlayerInput) return;
#if NX_UE_HAS_INPUT_KEY_EVENT_ARGS
	const auto Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
	auto Params = FInputKeyEventArgs::CreateSimulated(Key, Event, Value, Key.IsAnalog() ? 1 : 0, Device);
	Params.DeltaTime = Controller->GetWorld() ? Controller->GetWorld()->GetDeltaSeconds() : 0.016f;
	Controller->PlayerInput->InputKey(Params);
#elif NX_UE_HAS_FIND_FIRST_OBJECT
	if (Key.IsAnalog()) Controller->PlayerInput->InputKey(FInputKeyParams(Key, Value, Controller->GetWorld()->GetDeltaSeconds(), 1, Key.IsGamepadKey()));
	else Controller->PlayerInput->InputKey(FInputKeyParams(Key, Event, Value, Key.IsGamepadKey()));
#else
	if (Key.IsAnalog()) Controller->PlayerInput->InputAxis(Key, Value, Controller->GetWorld()->GetDeltaSeconds(), 1, Key.IsGamepadKey());
	else Controller->PlayerInput->InputKey(Key, Event, Value, Key.IsGamepadKey());
#endif
}

static void NexusFinishInput(const TSharedPtr<FNexusInputHold>& Hold, const FString& State, const FString& Reason)
{
	if (!Hold.IsValid() || Hold->bReleased) return;
	Hold->bReleased = true;
	APlayerController* Controller = Hold->Controller.Get();
	for (const auto& Input : Hold->Inputs)
	{
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
		if (Input.InputAction.IsValid())
		{
			ULocalPlayer* Player = Hold->Player.Get();
			auto* Subsystem = Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
			if (Subsystem) Subsystem->StopContinuousInputInjectionForAction(Input.InputAction.Get());
		}
		else
#endif
		if (Input.Key.IsValid()) NexusInjectKey(Controller, Input.Key, IE_Released, 0);
	}
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("bInputReleased"), true);
	Result->SetBoolField(TEXT("bGameResponseVerified"), false);
	FNexusEditorOperations::Finish(Hold->Operation, State, Result, Reason);
	GNexusInputHolds.Remove(Hold);
}

static void NexusInstallInputTicks()
{
	if (GNexusInputPreTick.IsValid()) return;
	GNexusInputPreTick = FWorldDelegates::OnWorldPreActorTick.AddLambda([](UWorld* World, ELevelTick, float)
	{
		const auto Holds = GNexusInputHolds;
		for (const auto& Hold : Holds) if (Hold->World.Get() == World)
			for (const auto& Input : Hold->Inputs) if (Input.Key.IsAnalog()) NexusInjectKey(Hold->Controller.Get(), Input.Key, IE_Pressed, Input.Value.X);
	});
	GNexusInputPostTick = FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* World, ELevelTick, float)
	{
		const auto Holds = GNexusInputHolds;
		for (const auto& Hold : Holds)
			if (Hold->World.Get() == World && !World->IsPaused() && FPlatformTime::Seconds() >= Hold->Deadline)
				NexusFinishInput(Hold, TEXT("Completed"), TEXT("Input hold completed after a game tick"));
	});
	GNexusInputCleanup = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool, bool)
	{
		const auto Holds = GNexusInputHolds;
		for (const auto& Hold : Holds) if (Hold->World.Get() == World) NexusFinishInput(Hold, TEXT("Interrupted"), TEXT("Play world ended"));
	});
	auto LeaseTick = FTickerDelegate::CreateLambda([](float)
	{
		const auto Holds = GNexusInputHolds;
		for (const auto& Hold : Holds)
			if (!Hold->Controller.IsValid() || !Hold->World.IsValid() || !Hold->Operation->IsRunning() || FPlatformTime::Seconds() >= Hold->LeaseDeadline)
				NexusFinishInput(Hold, TEXT("Interrupted"), TEXT("Input lease expired or player ended"));
		return true;
	});
#if NX_UE_HAS_FTSTICKER_HANDLE
	GNexusInputLeaseTicker = FTSTicker::GetCoreTicker().AddTicker(LeaseTick, 0.1f);
#else
	GNexusInputLeaseTicker = FTicker::GetCoreTicker().AddTicker(LeaseTick, 0.1f);
#endif
}

FCapabilityResult FNexusRuntimeInputUtils::Apply(const TSharedPtr<FJsonObject>& Arguments)
{
	const FNexusArgs Args(Arguments);
	const double Duration = Args.Num(TEXT("DurationSeconds"), 0);
	const int32 PlayerIndex = Args.Int(TEXT("LocalPlayerIndex"), 0);
	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
	if (!FMath::IsFinite(Duration) || Duration < 0 || Duration > 30 || PlayerIndex < 0 ||
		!Arguments->TryGetArrayField(TEXT("Inputs"), Inputs) || !Inputs || Inputs->Num() < 1 || Inputs->Num() > 16)
		return FCapabilityResult::MakeArgInvalid(TEXT("Inputs requires 1-16 entries, DurationSeconds 0-30, LocalPlayerIndex >= 0"));
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		UWorld* World = nullptr;
		if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (!Context.World() || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game)) continue;
			if (!Args.Str(TEXT("WorldPath")).IsEmpty() && Context.World()->GetPathName() != Args.Str(TEXT("WorldPath"))) continue;
			if (!Args.Str(TEXT("WorldName")).IsEmpty() && Context.World()->GetName() != Args.Str(TEXT("WorldName"))) continue;
			if (World) { Error = TEXT("Multiple play worlds: specify exact WorldPath from ControlPie Status"); return; }
			World = Context.World();
		}
		if (!World || World->IsPaused()) { Error = TEXT("An unpaused play world with a local player is required"); return; }
		auto* Instance = World->GetGameInstance();
		if (!Instance) { Error = TEXT("Play world has no GameInstance"); return; }
		const auto& Players = Instance->GetLocalPlayers();
		ULocalPlayer* Player = Players.IsValidIndex(PlayerIndex) ? Players[PlayerIndex] : nullptr;
		APlayerController* Controller = Player ? Player->GetPlayerController(World) : nullptr;
		if (!Controller || !Controller->PlayerInput) { Error = TEXT("LocalPlayerIndex has no PlayerController/PlayerInput in this world"); return; }
		auto Hold = MakeShared<FNexusInputHold>();
		Hold->World = World; Hold->Controller = Controller; Hold->Player = Player;
		TSet<FString> Identities;
		for (const auto& Value : *Inputs)
		{
			const auto Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Object) { Error = TEXT("Each Inputs entry must be an object"); return; }
			const FNexusArgs InputArgs(Object);
			const FString KeyName = InputArgs.Str(TEXT("Key"));
			const FString ActionPath = InputArgs.Str(TEXT("AssetPath"));
			if (KeyName.IsEmpty() == ActionPath.IsEmpty()) { Error = TEXT("Each entry requires exactly one Key or InputAction AssetPath"); return; }
			FNexusHeldInput Input;
			const double X = InputArgs.Num(TEXT("X"), 1), Y = InputArgs.Num(TEXT("Y"), 0), Z = InputArgs.Num(TEXT("Z"), 0);
			if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z) || FMath::Abs(X) > 1 || FMath::Abs(Y) > 1 || FMath::Abs(Z) > 1 || (X == 0 && Y == 0 && Z == 0))
			{ Error = TEXT("Input X/Y/Z must be finite values within [-1,1], with at least one nonzero axis"); return; }
			Input.Value = FVector(X, Y, Z);
			if (!KeyName.IsEmpty())
			{
				Input.Key = FKey(*KeyName); Input.Identity = Input.Key.ToString();
				if (!Input.Key.IsValid() || Input.Key.IsAxis2D() || Input.Key.IsAxis3D() || Y != 0 || Z != 0 ||
					(!Input.Key.IsAnalog() && X != 1) || Controller->PlayerInput->IsPressed(Input.Key))
				{ Error = TEXT("Key must be valid, scalar, not already pressed; digital keys require X=1 and Y=Z=0"); return; }
			}
			else
			{
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
				auto* Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
				auto* InputAction = LoadObject<UInputAction>(nullptr, *ActionPath);
				if (!Subsystem || !InputAction || Subsystem->HasContinuousInputInjectionForAction(InputAction))
				{ Error = TEXT("InputAction unavailable or already injected by another source"); return; }
				const int32 Dimensions = FMath::Max(1, static_cast<int32>(InputAction->ValueType));
				if ((Dimensions < 3 && Z != 0) || (Dimensions < 2 && Y != 0)) { Error = TEXT("Input value dimensions exceed InputAction type"); return; }
				Input.InputAction = InputAction; Input.Identity = InputAction->GetPathName();
#else
				Error = TEXT("InputAction injection requires enabled EnhancedInput and UE5.7+"); return;
#endif
			}
			if (Identities.Contains(Input.Identity)) { Error = TEXT("Duplicate input in this request"); return; }
			for (const auto& Existing : GNexusInputHolds)
				if (Existing->Controller.Get() == Controller)
					for (const auto& Held : Existing->Inputs) if (Held.Identity == Input.Identity) { Error = TEXT("Input already held by another operation"); return; }
			Identities.Add(Input.Identity); Hold->Inputs.Add(Input);
		}
		Hold->Operation = FNexusEditorOperations::Create(TEXT("NativePlayerInput"));
		if (!Hold->Operation) { Error = TEXT("Operation capacity reached"); return; }
		Hold->Deadline = FPlatformTime::Seconds() + Duration;
		Hold->LeaseDeadline = Hold->Deadline + 5;
		NexusInstallInputTicks();
		for (const auto& Input : Hold->Inputs)
		{
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
			if (Input.InputAction.IsValid())
			{
				auto* Action = Input.InputAction.Get();
				Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->StartContinuousInputInjectionForAction(Action, FInputActionValue(Action->ValueType, Input.Value), {}, {});
			}
			else
#endif
			if (!Input.Key.IsAnalog()) NexusInjectKey(Controller, Input.Key, IE_Pressed, Input.Value.X);
		}
		GNexusInputHolds.Add(Hold);
		Hold->Operation->Stop = [WeakHold = TWeakPtr<FNexusInputHold>(Hold)]()
		{
			if (const auto Pinned = WeakHold.Pin()) { NexusFinishInput(Pinned, TEXT("Cancelled"), TEXT("Input explicitly released")); return true; }
			return false;
		};
		auto Entry = Hold->Operation->Snapshot();
		Entry->SetStringField(TEXT("WorldName"), World->GetName());
		Entry->SetStringField(TEXT("WorldPath"), World->GetPathName());
		Entry->SetNumberField(TEXT("LocalPlayerIndex"), PlayerIndex);
		Entry->SetNumberField(TEXT("InputCount"), Hold->Inputs.Num());
		Entry->SetBoolField(TEXT("bGameResponseVerified"), false);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

FCapabilityResult FNexusRuntimeInputUtils::Query(const TSharedPtr<FJsonObject>& Arguments, bool bRelease)
{
	const FString Id = FNexusArgs(Arguments).Str(TEXT("OperationId"));
	const auto Operation = FNexusEditorOperations::Find(Id);
	if (!Operation || Operation->Backend != TEXT("NativePlayerInput")) return FCapabilityResult::MakeArgInvalid(TEXT("OperationId must identify this session's input operation"));
	if (bRelease && Operation->IsRunning()) FNexusEditorOperations::Cancel(Id);
	auto Entry = Operation->Snapshot();
	Entry->SetBoolField(TEXT("bInjecting"), Operation->IsRunning());
	TArray<TSharedPtr<FJsonValue>> States;
	for (const auto& Hold : GNexusInputHolds)
	{
		if (Hold->Operation != Operation) continue;
		for (const auto& Input : Hold->Inputs)
		{
			auto State = MakeShared<FJsonObject>();
			State->SetStringField(TEXT("Identity"), Input.Identity);
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
			if (Input.InputAction.IsValid())
			{
				ULocalPlayer* Player = Hold->Player.Get();
				auto* Subsystem = Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
				State->SetBoolField(TEXT("bActionInjectionActive"), Subsystem && Subsystem->HasContinuousInputInjectionForAction(Input.InputAction.Get()));
			}
			else
#endif
			{
				APlayerController* Controller = Hold->Controller.Get();
				State->SetBoolField(TEXT("bPlayerInputKeyDown"), Controller && Controller->PlayerInput && Controller->PlayerInput->IsPressed(Input.Key));
			}
			States.Add(MakeShared<FJsonValueObject>(State));
		}
	}
	Entry->SetArrayField(TEXT("Inputs"), States);
	FCapabilityResult Result; Result.Entries.Add(MakeShared<FJsonValueObject>(Entry)); return Result;
}

void FNexusRuntimeInputUtils::Shutdown()
{
	const auto Holds = GNexusInputHolds;
	for (const auto& Hold : Holds) NexusFinishInput(Hold, TEXT("Interrupted"), TEXT("MCP server stopped"));
	FWorldDelegates::OnWorldPreActorTick.Remove(GNexusInputPreTick); GNexusInputPreTick.Reset();
	FWorldDelegates::OnWorldPostActorTick.Remove(GNexusInputPostTick); GNexusInputPostTick.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(GNexusInputCleanup); GNexusInputCleanup.Reset();
#if NX_UE_HAS_FTSTICKER_HANDLE
	FTSTicker::GetCoreTicker().RemoveTicker(GNexusInputLeaseTicker);
#else
	FTicker::GetCoreTicker().RemoveTicker(GNexusInputLeaseTicker);
#endif
	GNexusInputLeaseTicker.Reset();
}
