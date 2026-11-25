// Copyright byteyang. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Utils/NexusRuntimeInputUtils.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"

static UEnhancedInputLocalPlayerSubsystem* NexusEnhancedInput(ULocalPlayer* Player)
{
	return Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

static UObject* NexusResolveInputAction(ULocalPlayer* Player, const FString& Path, const FVector& Value, FString& Error)
{
	const auto* Subsystem = NexusEnhancedInput(Player);
	auto* Action = LoadObject<UInputAction>(nullptr, *Path);
	if (!Subsystem || !Action || Subsystem->HasContinuousInputInjectionForAction(Action))
	{
		Error = TEXT("InputAction unavailable or already injected by another source");
		return nullptr;
	}
	const int32 Dimensions = FMath::Max(1, static_cast<int32>(Action->ValueType));
	if ((Dimensions < 3 && Value.Z != 0) || (Dimensions < 2 && Value.Y != 0))
	{
		Error = TEXT("Input value dimensions exceed InputAction type");
		return nullptr;
	}
	return Action;
}
#endif

class FNexusLinkEnhancedInputModule final : public IModuleInterface
{
	void StartupModule() override
	{
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
		FNexusRuntimeInputUtils::ResolveAction = &NexusResolveInputAction;
		FNexusRuntimeInputUtils::StartAction = [](ULocalPlayer* Player, UObject* Object, const FVector& Value)
		{
			if (auto* Subsystem = NexusEnhancedInput(Player))
			{
				const auto* Action = CastChecked<UInputAction>(Object);
				Subsystem->StartContinuousInputInjectionForAction(Action, FInputActionValue(Action->ValueType, Value), {}, {});
			}
		};
		FNexusRuntimeInputUtils::StopAction = [](ULocalPlayer* Player, UObject* Object)
		{
			if (auto* Subsystem = NexusEnhancedInput(Player))
				Subsystem->StopContinuousInputInjectionForAction(CastChecked<UInputAction>(Object));
		};
		FNexusRuntimeInputUtils::IsActionActive = [](ULocalPlayer* Player, UObject* Object)
		{
			const auto* Subsystem = NexusEnhancedInput(Player);
			return Subsystem && Subsystem->HasContinuousInputInjectionForAction(CastChecked<UInputAction>(Object));
		};
#endif
	}

	void ShutdownModule() override
	{
#if WITH_ENHANCED_INPUT && NX_UE_HAS_CONTINUOUS_INPUT_INJECTION
		FNexusRuntimeInputUtils::Shutdown();
		FNexusRuntimeInputUtils::ResolveAction = nullptr;
		FNexusRuntimeInputUtils::StartAction = nullptr;
		FNexusRuntimeInputUtils::StopAction = nullptr;
		FNexusRuntimeInputUtils::IsActionActive = nullptr;
#endif
	}
};

IMPLEMENT_MODULE(FNexusLinkEnhancedInputModule, NexusLinkEnhancedInput)
