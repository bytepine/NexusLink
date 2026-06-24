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

void FGetRuntimeActorAbilitySystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_actor_ability_system");
	Out.Description = TEXT("PIE 读 Actor ASC 快照。sections=abilities|effects|attributes。写用 interact。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Runtime, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("asc"), TEXT("ability"), TEXT("runtime"), TEXT("pie") };
	Out.RelatedCapabilities = { TEXT("interact_runtime_actor_ability_system"), TEXT("get_gameplay_tags"), TEXT("get_runtime_actor_property") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("PIE 中读 Actor 技能/GE/属性快照；写用 interact_runtime_actor_ability_system");
}

TSharedPtr<FJsonObject> FGetRuntimeActorAbilitySystemCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Actor 名称（可选；省略则取 World 中首个带 ASC 的 Pawn/Actor）")))
		.Build();
}

TArray<FString> FGetRuntimeActorAbilitySystemCapability::GetSectionNames() const
{
	return { TEXT("abilities"), TEXT("effects"), TEXT("attributes") };
}

TArray<FString> FGetRuntimeActorAbilitySystemCapability::GetDefaultSectionNames() const
{
	return { TEXT("abilities"), TEXT("effects"), TEXT("attributes") };
}

bool FGetRuntimeActorAbilitySystemCapability::PrepareEntry(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>&       OutEntry,
	void*&                         OutTargetOpaque,
	FString&                       OutError) const
{
	FString WorldError;
	UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(WorldError);
	if (!World) { OutError = WorldError; return false; }

	FString ActorName;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("actorName"), ActorName);

	UAbilitySystemComponent* ASC = nullptr;

	if (ActorName.IsEmpty())
	{
		// 遍历 World 查找首个带 ASC 的 Pawn 或 Actor
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			ASC = FNexusGasUtils::FindAbilitySystemComponent(*It);
			if (ASC)
			{
				OutEntry->SetStringField(TEXT("actorName"), (*It)->GetName());
				break;
			}
		}
		if (!ASC) { OutError = TEXT("World 中未找到带 AbilitySystemComponent 的 Actor"); return false; }
	}
	else
	{
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor) { OutError = FString::Printf(TEXT("Actor 未找到: %s"), *ActorName); return false; }
		ASC = FNexusGasUtils::FindAbilitySystemComponent(Actor);
		if (!ASC) { OutError = FString::Printf(TEXT("Actor '%s' 无 AbilitySystemComponent"), *ActorName); return false; }
		OutEntry->SetStringField(TEXT("actorName"), ActorName);
	}

	OutTargetOpaque = static_cast<void*>(ASC);
	return true;
}

void FGetRuntimeActorAbilitySystemCapability::ExecuteSection(
	const FString&                 SectionName,
	const TSharedPtr<FJsonObject>& Args,
	void*                          TargetOpaque,
	TSharedPtr<FJsonObject>&       InOutDetail,
	FString&                       OutError) const
{
	UAbilitySystemComponent* ASC = static_cast<UAbilitySystemComponent*>(TargetOpaque);
	if (!ASC) { OutError = TEXT("无效的 ASC 指针"); return; }

	if (SectionName == TEXT("abilities"))
	{
		TArray<TSharedPtr<FJsonValue>> Abilities;
		TArray<FGameplayAbilitySpec> Specs = ASC->GetActivatableAbilities();
		for (const FGameplayAbilitySpec& Spec : Specs)
		{
			TSharedPtr<FJsonObject> AbObj = MakeShared<FJsonObject>();
			if (Spec.Ability)
			{
				AbObj->SetStringField(TEXT("abilityClass"), Spec.Ability->GetClass()->GetName());
				AbObj->SetNumberField(TEXT("level"),        Spec.Level);
				AbObj->SetBoolField(TEXT("isActive"),       Spec.IsActive());
				AbObj->SetNumberField(TEXT("inputID"),      Spec.InputID);
			}
			Abilities.Add(MakeShared<FJsonValueObject>(AbObj));
		}
		InOutDetail->SetArrayField(TEXT("abilities"), Abilities);
		InOutDetail->SetNumberField(TEXT("count"),    Abilities.Num());
	}
	else if (SectionName == TEXT("effects"))
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
				GEObj->SetStringField(TEXT("effectClass"), ActiveGE->Spec.Def->GetClass()->GetName());
			}
			// StackCount 在 UE4 为 public，在 UE5.8 变为 private，用反射统一访问
			{
				int32 StackCount = 0;
				FNumericProperty* SCProp = FindFProperty<FNumericProperty>(
					FGameplayEffectSpec::StaticStruct(), TEXT("StackCount"));
				if (SCProp)
					StackCount = (int32)SCProp->GetSignedIntPropertyValue(
						SCProp->ContainerPtrToValuePtr<void>(&ActiveGE->Spec));
				GEObj->SetNumberField(TEXT("stackCount"), StackCount);
			}
			GEObj->SetNumberField(TEXT("level"),          ActiveGE->Spec.GetLevel());
			const float RemainingTime = ASC->GetGameplayEffectDuration(Handle);
			if (RemainingTime >= 0.f)
				GEObj->SetNumberField(TEXT("remainingTime"), RemainingTime);
			Effects.Add(MakeShared<FJsonValueObject>(GEObj));
		}
		InOutDetail->SetArrayField(TEXT("effects"), Effects);
		InOutDetail->SetNumberField(TEXT("count"),  Effects.Num());
	}
	else if (SectionName == TEXT("attributes"))
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
				AObj->SetStringField(TEXT("attributeSet"),  AttrClass->GetName());
				AObj->SetNumberField(TEXT("baseValue"),     ASC->GetNumericAttributeBase(Attr));
				AObj->SetNumberField(TEXT("currentValue"),  ASC->GetNumericAttribute(Attr));
				Attrs.Add(MakeShared<FJsonValueObject>(AObj));
			}
		}
		InOutDetail->SetArrayField(TEXT("attributes"), Attrs);
		InOutDetail->SetNumberField(TEXT("count"),     Attrs.Num());
	}
}

REGISTER_MCP_CAPABILITY(FGetRuntimeActorAbilitySystemCapability)

#endif // WITH_GAS
