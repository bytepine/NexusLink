// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusGasDiagnostics.h"

#if WITH_GAS
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformProcess.h"

struct FNexusGasCapture final
{
	TWeakObjectPtr<UAbilitySystemComponent> Asc;
	double ExpiresAt = 0;
	int32 Sequence = 0;
	int32 Dropped = 0;
	TArray<TSharedPtr<FJsonValue>> Events;
	FDelegateHandle Activated, Committed, Failed, Ended;

	~FNexusGasCapture()
	{
		if (UAbilitySystemComponent* Component = Asc.Get())
		{
			Component->AbilityActivatedCallbacks.Remove(Activated);
			Component->AbilityCommittedCallbacks.Remove(Committed);
			Component->AbilityFailedCallbacks.Remove(Failed);
			Component->OnAbilityEnded.Remove(Ended);
		}
	}

	void Record(const TCHAR* Kind, UGameplayAbility* Ability, const FGameplayTagContainer& Reasons,
		FGameplayAbilitySpecHandle EndedHandle = FGameplayAbilitySpecHandle())
	{
		if (!Ability || !Asc.IsValid() || FPlatformTime::Seconds() >= ExpiresAt) return;
		TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetNumberField(TEXT("Sequence"), ++Sequence);
		Event->SetStringField(TEXT("Event"), Kind);
		Event->SetStringField(TEXT("Time"), FDateTime::UtcNow().ToIso8601());
		Event->SetStringField(TEXT("AbilityPath"), Ability->GetClass()->GetPathName());
		Event->SetArrayField(TEXT("FailureTags"), FNexusGasUtils::SerializeTagContainer(Reasons));
		// 非实例化技能的 CDO 不携带当前句柄，不能把类名猜成一次激活的身份。
		const bool bInstanced = Ability->IsInstantiated();
		FGameplayAbilitySpecHandle Handle = EndedHandle.IsValid() ? EndedHandle
			: (bInstanced ? Ability->GetCurrentAbilitySpecHandle() : FGameplayAbilitySpecHandle());
		if (!Handle.IsValid() && Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor)
		{
			// 首次激活检查失败时引擎可能传 CDO；只在该类唯一授予时关联句柄。
			int32 Matches = 0;
			for (const FGameplayAbilitySpec& Spec : Asc->GetActivatableAbilities())
				if (Spec.Ability && Spec.Ability->GetClass() == Ability->GetClass()) { Handle = Spec.Handle; ++Matches; }
			if (Matches != 1) Handle = FGameplayAbilitySpecHandle();
		}
		Event->SetStringField(TEXT("SpecHandle"), Handle.ToString());
		Event->SetBoolField(TEXT("bIdentityKnown"), Handle.IsValid());
		if (bInstanced)
			Event->SetStringField(TEXT("PredictionKey"), Ability->GetCurrentActivationInfo().GetActivationPredictionKey().ToString());
		if (Events.Num() == 256) { Events.RemoveAt(0); ++Dropped; }
		Events.Add(MakeShared<FJsonValueObject>(Event));
	}
};

struct FNexusGasCaptureStore final
{
	TMap<FString, TSharedPtr<FNexusGasCapture>> Captures;
#if NX_UE_HAS_FTSTICKER
	FTSTicker::FDelegateHandle Ticker;
#else
	FDelegateHandle Ticker;
#endif
	FDelegateHandle WorldCleanup;

	static FNexusGasCaptureStore& Get() { static FNexusGasCaptureStore Store; return Store; }
	void Prune()
	{
		for (auto It = Captures.CreateIterator(); It; ++It)
			if (!It.Value()->Asc.IsValid() || FPlatformTime::Seconds() >= It.Value()->ExpiresAt) It.RemoveCurrent();
	}
	void EnsureTicker()
	{
		if (Ticker.IsValid()) return;
#if NX_UE_HAS_FTSTICKER
		Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) { Prune(); return true; }), 1.f);
#else
		Ticker = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) { Prune(); return true; }), 1.f);
#endif
		WorldCleanup = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* World, bool, bool)
		{
			for (auto It = Captures.CreateIterator(); It; ++It)
				if (!It.Value()->Asc.IsValid() || It.Value()->Asc->GetWorld() == World) It.RemoveCurrent();
		});
	}
};

FGameplayAbilitySpec* FNexusGasDiagnostics::FindSpec(UAbilitySystemComponent* Asc, const TSharedPtr<FJsonObject>& Args, FString& Error)
{
	FString Handle, Path;
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("SpecHandle"), Handle); Args->TryGetStringField(TEXT("AbilityPath"), Path); }
	if ((Handle.IsEmpty() && Path.IsEmpty()) || (!Handle.IsEmpty() && !Path.IsEmpty()))
	{
		Error = TEXT("Pass exactly one of SpecHandle or AbilityPath"); return nullptr;
	}
	FGameplayAbilitySpec* Match = nullptr;
	for (FGameplayAbilitySpec& Spec : Asc->GetActivatableAbilities())
	{
		if (!Spec.Ability) continue;
		const FString ClassPath = Spec.Ability->GetClass()->GetPathName();
		if ((!Handle.IsEmpty() && Spec.Handle.ToString() == Handle)
			|| (!Path.IsEmpty() && (Path == ClassPath || Path + TEXT("_C") == ClassPath)))
		{
			if (Match) { Error = TEXT("Ambiguous AbilityPath; select a SpecHandle"); return nullptr; }
			Match = &Spec;
		}
	}
	return Match;
}

TSharedPtr<FJsonObject> FNexusGasDiagnostics::Context(UAbilitySystemComponent* Asc)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	UWorld* World = Asc->GetWorld();
	AActor* Avatar = Asc->GetAvatarActor();
	AActor* Owner = Asc->GetOwnerActor();
	APawn* Pawn = Cast<APawn>(Avatar);
	APlayerState* PlayerState = Pawn ? Pawn->GetPlayerState() : Cast<APlayerState>(Owner);
	Result->SetNumberField(TEXT("ProcessId"), FPlatformProcess::GetCurrentProcessId());
	Result->SetNumberField(TEXT("WorldId"), World ? World->GetUniqueID() : 0);
	Result->SetStringField(TEXT("WorldName"), GetPathNameSafe(World));
	Result->SetStringField(TEXT("Owner"), GetNameSafe(Owner));
	Result->SetStringField(TEXT("Avatar"), GetNameSafe(Avatar));
	Result->SetStringField(TEXT("AscId"), FNexusRuntimeUtils::GetObjectIdentity(Asc));
	Result->SetStringField(TEXT("OwnerId"), FNexusRuntimeUtils::GetObjectIdentity(Owner));
	Result->SetStringField(TEXT("AvatarId"), FNexusRuntimeUtils::GetObjectIdentity(Avatar));
	Result->SetStringField(TEXT("WorldIdentity"), FNexusRuntimeUtils::GetObjectIdentity(World));
	Result->SetStringField(TEXT("OwnerClass"), Owner ? Owner->GetClass()->GetPathName() : FString());
	Result->SetStringField(TEXT("AvatarClass"), Avatar ? Avatar->GetClass()->GetPathName() : FString());
	Result->SetNumberField(TEXT("PlayerId"), PlayerState ? PlayerState->GetPlayerId() : -1);
	Result->SetBoolField(TEXT("bLocallyControlled"), Pawn && Pawn->IsLocallyControlled());
	Result->SetBoolField(TEXT("bAuthority"), Owner && Owner->HasAuthority());
	Result->SetBoolField(TEXT("bActorInfoReady"), Owner && Avatar);
	Result->SetNumberField(TEXT("NetMode"), World ? static_cast<int32>(World->GetNetMode()) : -1);
	Result->SetNumberField(TEXT("LocalRole"), Avatar ? static_cast<int32>(Avatar->GetLocalRole()) : -1);
	return Result;
}

void FNexusGasDiagnostics::Diagnose(UAbilitySystemComponent* Asc, const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Detail, FString& Error)
{
	FGameplayAbilitySpec* Spec = FindSpec(Asc, Args, Error);
	if (!Error.IsEmpty()) return;
	Detail->SetBoolField(TEXT("bGranted"), Spec != nullptr);
	Detail->SetStringField(TEXT("Evaluation"), TEXT("SnapshotOnly"));
	Detail->SetStringField(TEXT("CustomChecks"), TEXT("Unknown; reproduce with activation capture for engine failure tags"));
	Detail->SetStringField(TEXT("CostCheck"), TEXT("Unknown; no project check functions invoked"));
	if (!Spec) { Detail->SetStringField(TEXT("Reason"), TEXT("AbilityNotGrantedOrNotVisible")); return; }
	Detail->SetStringField(TEXT("SpecHandle"), Spec->Handle.ToString());
	Detail->SetStringField(TEXT("AbilityPath"), Spec->Ability->GetClass()->GetPathName());
	FGameplayTagContainer Owned, Missing, Blocking;
	Asc->GetOwnedGameplayTags(Owned);
	const FGameplayTagContainer* Required = NxGasPropPtr<FGameplayTagContainer>(Spec->Ability, TEXT("ActivationRequiredTags"));
	const FGameplayTagContainer* Blocked = NxGasPropPtr<FGameplayTagContainer>(Spec->Ability, TEXT("ActivationBlockedTags"));
	if (Required) for (const FGameplayTag& Tag : *Required) if (!Owned.HasTag(Tag)) Missing.AddTag(Tag);
	if (Blocked) for (const FGameplayTag& Tag : *Blocked) if (Owned.HasTag(Tag)) Blocking.AddTag(Tag);
	Detail->SetBoolField(TEXT("bTagConfigurationKnown"), Required && Blocked);
	Detail->SetArrayField(TEXT("MissingRequiredTags"), FNexusGasUtils::SerializeTagContainer(Missing));
	Detail->SetArrayField(TEXT("BlockingOwnedTags"), FNexusGasUtils::SerializeTagContainer(Blocking));
	// 只按配置的冷却 GE 类匹配活跃效果，不调用可被项目覆盖的 GetCooldownTags。
	UClass* CooldownClass = NxGasGetClassProp(Spec->Ability, TEXT("CooldownGameplayEffectClass"));
	UClass* CostClass = NxGasGetClassProp(Spec->Ability, TEXT("CostGameplayEffectClass"));
	Detail->SetStringField(TEXT("CostGE"), GetPathNameSafe(CostClass));
	Detail->SetStringField(TEXT("CooldownGE"), GetPathNameSafe(CooldownClass));
	double Remaining = 0;
	for (const FActiveGameplayEffectHandle& Handle : Asc->GetActiveEffects(FGameplayEffectQuery()))
	{
		const FActiveGameplayEffect* Effect = Asc->GetActiveGameplayEffect(Handle);
		if (CooldownClass && Effect && Effect->Spec.Def && Effect->Spec.Def->GetClass() == CooldownClass)
			Remaining = FMath::Max(Remaining, static_cast<double>(Effect->GetTimeRemaining(Asc->GetWorld()->GetTimeSeconds())));
	}
	Detail->SetNumberField(TEXT("ConfiguredCooldownRemaining"), Remaining);
}

FString FNexusGasDiagnostics::StartCapture(UAbilitySystemComponent* Asc, FString& Error)
{
	FNexusGasCaptureStore& Store = FNexusGasCaptureStore::Get();
	Store.Prune();
	if (Store.Captures.Num() >= 16) { Error = TEXT("Activation capture limit (16); stop existing captures"); return FString(); }
	Store.EnsureTicker();
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TSharedPtr<FNexusGasCapture> Capture = MakeShared<FNexusGasCapture>();
	Capture->Asc = Asc;
	Capture->ExpiresAt = FPlatformTime::Seconds() + 120;
	TWeakPtr<FNexusGasCapture> Weak = Capture;
	Capture->Activated = Asc->AbilityActivatedCallbacks.AddLambda([Weak](UGameplayAbility* Ability)
		{ if (auto Pinned = Weak.Pin()) Pinned->Record(TEXT("Activated"), Ability, FGameplayTagContainer()); });
	Capture->Committed = Asc->AbilityCommittedCallbacks.AddLambda([Weak](UGameplayAbility* Ability)
		{ if (auto Pinned = Weak.Pin()) Pinned->Record(TEXT("Committed"), Ability, FGameplayTagContainer()); });
	Capture->Failed = Asc->AbilityFailedCallbacks.AddLambda([Weak](const UGameplayAbility* Ability, const FGameplayTagContainer& Tags)
		{ if (auto Pinned = Weak.Pin()) Pinned->Record(TEXT("Failed"), const_cast<UGameplayAbility*>(Ability), Tags); });
	Capture->Ended = Asc->OnAbilityEnded.AddLambda([Weak](const FAbilityEndedData& Data)
		{ if (auto Pinned = Weak.Pin()) Pinned->Record(TEXT("Ended"), Data.AbilityThatEnded, FGameplayTagContainer(), Data.AbilitySpecHandle); });
	Store.Captures.Add(Id, Capture);
	return Id;
}

bool FNexusGasDiagnostics::ReadCapture(UAbilitySystemComponent* Asc, const FString& Id, TSharedPtr<FJsonObject>& Detail, FString& Error)
{
	FNexusGasCaptureStore& Store = FNexusGasCaptureStore::Get();
	Store.Prune();
	const TSharedPtr<FNexusGasCapture>* Found = Store.Captures.Find(Id);
	if (!Found || (*Found)->Asc.Get() != Asc) { Error = TEXT("Unknown, expired, or different Actor capture"); return false; }
	Detail->SetStringField(TEXT("CaptureId"), Id);
	Detail->SetNumberField(TEXT("Dropped"), (*Found)->Dropped);
	Detail->SetNumberField(TEXT("LatestSequence"), (*Found)->Sequence);
	TArray<TSharedPtr<FJsonValue>> Snapshot;
	TArray<TSharedPtr<FJsonValue>> Types;
	FGameplayTagContainer Failures;
	for (const TSharedPtr<FJsonValue>& Value : (*Found)->Events)
	{
		const TSharedPtr<FJsonObject> Event = Value->AsObject();
		// 响应压缩会移除共同字段；事件只有标量和字符串数组，复制字段表即可隔离缓存。
		Snapshot.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>(*Event)));
		Types.Add(MakeShared<FJsonValueString>(Event->GetStringField(TEXT("Event"))));
		const TArray<TSharedPtr<FJsonValue>>& Tags = Event->GetArrayField(TEXT("FailureTags"));
		for (const TSharedPtr<FJsonValue>& Tag : Tags)
			Failures.AddTag(FGameplayTag::RequestGameplayTag(FName(*Tag->AsString()), false));
	}
	Detail->SetArrayField(TEXT("Events"), Snapshot);
	Detail->SetArrayField(TEXT("EventTypes"), Types);
	Detail->SetArrayField(TEXT("FailureTags"), FNexusGasUtils::SerializeTagContainer(Failures));
	return true;
}

bool FNexusGasDiagnostics::StopCapture(UAbilitySystemComponent* Asc, const FString& Id, FString& Error)
{
	TSharedPtr<FJsonObject> Unused = MakeShared<FJsonObject>();
	if (!ReadCapture(Asc, Id, Unused, Error)) return false;
	FNexusGasCaptureStore::Get().Captures.Remove(Id);
	return true;
}

void FNexusGasDiagnostics::Shutdown()
{
	FNexusGasCaptureStore& Store = FNexusGasCaptureStore::Get();
	Store.Captures.Empty();
	FWorldDelegates::OnWorldCleanup.Remove(Store.WorldCleanup);
	Store.WorldCleanup.Reset();
#if NX_UE_HAS_FTSTICKER
	FTSTicker::GetCoreTicker().RemoveTicker(Store.Ticker);
#else
	FTicker::GetCoreTicker().RemoveTicker(Store.Ticker);
#endif
	Store.Ticker.Reset();
}
#endif
