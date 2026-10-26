// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/GAS/NexusGetRuntimeActorAbilitySystemCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusGasDiagnostics.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "NexusMcpTool.h"

// ── Capability 元数据 ─────────────────────────────────────────────────────────

void FNexusGetRuntimeActorAbilitySystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeActorAbilitySystem");
	Out.Description = TEXT("Read ASC state, activation diagnostics and captured events in PIE/Game. Never invokes custom activation checks.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Runtime, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("asc"), TEXT("ability"), TEXT("Runtime"), TEXT("PIE"), TEXT("activation"), TEXT("failure"), TEXT("prediction"), TEXT("cooldown") };
	Out.RelatedCapabilities = { TEXT("InteractRuntimeActorAbilitySystem"), TEXT("GetGameplayTags"), TEXT("GetRuntimeActorProperty") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Inspect GAS state and activation evidence");
}

TSharedPtr<FJsonObject> FNexusGetRuntimeActorAbilitySystemCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor name (optional; first ASC in World if omitted)")))
		.Prop(TEXT("SpecHandle"), FNexusSchema::Str(TEXT("Granted ability handle from Abilities; exclusive with AbilityPath")))
		.Prop(TEXT("AbilityPath"), FNexusSchema::Str(TEXT("Exact ability class path; exclusive with SpecHandle")))
		.Prop(TEXT("CaptureId"), FNexusSchema::Str(TEXT("Activation capture returned by StartActivationCapture")))
		.Build();
}

TArray<FString> FNexusGetRuntimeActorAbilitySystemCapability::GetSectionNames() const
{
	return { TEXT("Abilities"), TEXT("Effects"), TEXT("Attributes"), TEXT("Context"), TEXT("Tags"), TEXT("ActivationDiagnostics"), TEXT("ActivationEvents") };
}

TArray<FString> FNexusGetRuntimeActorAbilitySystemCapability::GetDefaultSectionNames() const
{
	return { TEXT("Abilities"), TEXT("Effects"), TEXT("Attributes") };
}

bool FNexusGetRuntimeActorAbilitySystemCapability::PrepareEntry(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>&       OutEntry,
	void*&                         OutTargetOpaque,
	FString&                       OutError) const
{
	FString WorldError;
	UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(WorldError);
	if (!World) { OutError = WorldError; return false; }

	FString ActorName;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("ActorName"), ActorName);
	const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
	if (ActorName.IsEmpty() && Args.IsValid() && Args->TryGetArrayField(TEXT("Sections"), Sections))
		for (const TSharedPtr<FJsonValue>& Section : *Sections)
			if (Section->AsString() == TEXT("Context") || Section->AsString() == TEXT("Tags")
				|| Section->AsString().StartsWith(TEXT("Activation")))
			{ OutError = TEXT("Diagnostic sections require explicit ActorName"); return false; }

	UAbilitySystemComponent* ASC = nullptr;

	if (ActorName.IsEmpty())
	{
		// 遍历 World 查找首个带 ASC 的 Pawn 或 Actor
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			ASC = FNexusGasUtils::FindAbilitySystemComponent(*It);
			if (ASC)
			{
				OutEntry->SetStringField(TEXT("ActorName"), (*It)->GetName());
				break;
			}
		}
		if (!ASC) { OutError = TEXT("No Actor with AbilitySystemComponent found in World"); return false; }
	}
	else
	{
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return false; }
		ASC = FNexusGasUtils::FindAbilitySystemComponent(Actor);
		if (!ASC) { OutError = FString::Printf(TEXT("Actor '%s' has no AbilitySystemComponent"), *ActorName); return false; }
		OutEntry->SetStringField(TEXT("ActorName"), ActorName);
	}

	OutTargetOpaque = static_cast<void*>(ASC);
	return true;
}

void FNexusGetRuntimeActorAbilitySystemCapability::ExecuteSection(
	const FString&                 SectionName,
	const TSharedPtr<FJsonObject>& Args,
	void*                          TargetOpaque,
	TSharedPtr<FJsonObject>&       InOutDetail,
	FString&                       OutError) const
{
	UAbilitySystemComponent* ASC = static_cast<UAbilitySystemComponent*>(TargetOpaque);
	if (!ASC) { OutError = TEXT("Invalid ASC pointer"); return; }
	bool bAll = false;
	const TArray<TSharedPtr<FJsonValue>>* RequestedSections = nullptr;
	if (Args.IsValid() && Args->TryGetArrayField(TEXT("Sections"), RequestedSections))
		for (const TSharedPtr<FJsonValue>& Section : *RequestedSections)
			bAll |= Section->AsString().Equals(TEXT("All"), ESearchCase::IgnoreCase);
	if (SectionName == TEXT("Context"))
	{
		InOutDetail->SetObjectField(TEXT("Context"), FNexusGasDiagnostics::Context(ASC));
		return;
	}
	if (SectionName == TEXT("Tags"))
	{
		FGameplayTagContainer Owned, Blocked;
		ASC->GetOwnedGameplayTags(Owned);
		ASC->GetBlockedAbilityTags(Blocked);
		InOutDetail->SetArrayField(TEXT("OwnedTags"), FNexusGasUtils::SerializeTagContainer(Owned));
		InOutDetail->SetArrayField(TEXT("BlockedAbilityTags"), FNexusGasUtils::SerializeTagContainer(Blocked));
		return;
	}
	if (SectionName == TEXT("ActivationDiagnostics"))
	{
		// 旧 All 查询无需新增选择器；只有明确请求诊断或传入选择器时才解析它。
		if (bAll && !Args->HasField(TEXT("SpecHandle")) && !Args->HasField(TEXT("AbilityPath"))) return;
		TSharedPtr<FJsonObject> Diagnosis = MakeShared<FJsonObject>();
		FNexusGasDiagnostics::Diagnose(ASC, Args, Diagnosis, OutError);
		InOutDetail->SetObjectField(TEXT("ActivationDiagnostics"), Diagnosis);
		return;
	}
	if (SectionName == TEXT("ActivationEvents"))
	{
		FString Id;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("CaptureId"), Id);
		if (bAll && Id.IsEmpty()) return;
		FNexusGasDiagnostics::ReadCapture(ASC, Id, InOutDetail, OutError);
		return;
	}

	if (SectionName == TEXT("Abilities"))
	{
		TArray<TSharedPtr<FJsonValue>> Abilities;
		TArray<FGameplayAbilitySpec> Specs = ASC->GetActivatableAbilities();
		for (const FGameplayAbilitySpec& Spec : Specs)
		{
			TSharedPtr<FJsonObject> AbObj = MakeShared<FJsonObject>();
			if (Spec.Ability)
			{
				AbObj->SetStringField(TEXT("AbilityClass"), Spec.Ability->GetClass()->GetName());
				AbObj->SetStringField(TEXT("AbilityPath"), Spec.Ability->GetClass()->GetPathName());
				AbObj->SetStringField(TEXT("SpecHandle"), Spec.Handle.ToString());
				AbObj->SetNumberField(TEXT("InstancingPolicy"), static_cast<int32>(Spec.Ability->GetInstancingPolicy()));
				AbObj->SetNumberField(TEXT("NetExecutionPolicy"), static_cast<int32>(Spec.Ability->GetNetExecutionPolicy()));
				AbObj->SetNumberField(TEXT("Level"),        Spec.Level);
				AbObj->SetBoolField(TEXT("bIsActive"),       Spec.IsActive());
				AbObj->SetNumberField(TEXT("InputId"),      Spec.InputID);
			}
			Abilities.Add(MakeShared<FJsonValueObject>(AbObj));
		}
		InOutDetail->SetArrayField(TEXT("Abilities"), Abilities);
		InOutDetail->SetNumberField(TEXT("Count"),    Abilities.Num());
	}
	else if (SectionName == TEXT("Effects"))
	{
		TArray<TSharedPtr<FJsonValue>> Effects;
		TArray<FActiveGameplayEffectHandle> AllHandles = ASC->GetActiveEffects(FGameplayEffectQuery());
		for (const FActiveGameplayEffectHandle& Handle : AllHandles)
		{
			if (!Handle.IsValid()) continue;
			const FActiveGameplayEffect* ActiveGE = ASC->GetActiveGameplayEffect(Handle);
			if (!ActiveGE) continue;

			TSharedPtr<FJsonObject> GEObj = MakeShared<FJsonObject>();
			if (ActiveGE->Spec.Def)
			{
				GEObj->SetStringField(TEXT("EffectClass"), ActiveGE->Spec.Def->GetClass()->GetName());
			}
			// StackCount 在 UE4 为 public，在 UE5.8 变为 private，用反射统一访问
			{
				int32 StackCount = 0;
				FNumericProperty* SCProp = FindFProperty<FNumericProperty>(
					FGameplayEffectSpec::StaticStruct(), TEXT("StackCount"));
				if (SCProp)
					StackCount = (int32)SCProp->GetSignedIntPropertyValue(
						SCProp->ContainerPtrToValuePtr<void>(&ActiveGE->Spec));
				GEObj->SetNumberField(TEXT("StackCount"), StackCount);
			}
			GEObj->SetNumberField(TEXT("Level"),          ActiveGE->Spec.GetLevel());
			const float RemainingTime = ASC->GetGameplayEffectDuration(Handle);
			if (RemainingTime >= 0.f)
				GEObj->SetNumberField(TEXT("RemainingTime"), RemainingTime);
			Effects.Add(MakeShared<FJsonValueObject>(GEObj));
		}
		InOutDetail->SetArrayField(TEXT("Effects"), Effects);
		InOutDetail->SetNumberField(TEXT("Count"),  Effects.Num());
	}
	else if (SectionName == TEXT("Attributes"))
	{
		TArray<TSharedPtr<FJsonValue>> Attrs;
		// 枚举所有已注册 AttributeSet 的属性
		for (const UAttributeSet* AttrSet : ASC->GetSpawnedAttributes())
		{
			if (!AttrSet) continue;
			UClass* AttrClass = AttrSet->GetClass();
			for (TFieldIterator<FProperty> PropIt(AttrClass); PropIt; ++PropIt)
			{
				FStructProperty* StructProp = CastField<FStructProperty>(*PropIt);
				if (!StructProp || !StructProp->Struct) continue;
				if (!StructProp->Struct->IsChildOf(FGameplayAttributeData::StaticStruct())) continue;

				const FGameplayAttributeData* AttrData =
					StructProp->ContainerPtrToValuePtr<FGameplayAttributeData>(AttrSet);
				if (!AttrData) continue;

				FGameplayAttribute Attr(StructProp);
				TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
				AObj->SetStringField(TEXT("name"),          PropIt->GetName());
				AObj->SetStringField(TEXT("AttributeSet"),  AttrClass->GetName());
				AObj->SetNumberField(TEXT("BaseValue"),     ASC->GetNumericAttributeBase(Attr));
				AObj->SetNumberField(TEXT("CurrentValue"),  ASC->GetNumericAttribute(Attr));
				Attrs.Add(MakeShared<FJsonValueObject>(AObj));
			}
		}
		InOutDetail->SetArrayField(TEXT("Attributes"), Attrs);
		InOutDetail->SetNumberField(TEXT("Count"),     Attrs.Num());
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetRuntimeActorAbilitySystemCapability)

#endif // WITH_GAS
