// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/GAS/NexusGetRuntimeActorAbilitySystemCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusGasUtils.h"
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
	Out.Description = TEXT("Read Actor ASC snapshot in PIE. Sections=abilities|effects|attributes. Writes via interact.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Runtime, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("asc"), TEXT("ability"), TEXT("Runtime"), TEXT("PIE") };
	Out.RelatedCapabilities = { TEXT("InteractRuntimeActorAbilitySystem"), TEXT("GetGameplayTags"), TEXT("GetRuntimeActorProperty") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Read Actor abilities/GE/attributes in PIE; write via InteractRuntimeActorAbilitySystem");
}

TSharedPtr<FJsonObject> FNexusGetRuntimeActorAbilitySystemCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor name (optional; first ASC in World if omitted)")))
		.Build();
}

TArray<FString> FNexusGetRuntimeActorAbilitySystemCapability::GetSectionNames() const
{
	return { TEXT("Abilities"), TEXT("Effects"), TEXT("Attributes") };
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
