// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/GAS/NexusInteractRuntimeActorAbilitySystemCapability.h"

#if WITH_GAS

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusGasUtils.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "NexusMcpTool.h"

// ── Capability 元数据 ─────────────────────────────────────────────────────────

void FInteractRuntimeActorAbilitySystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_ability_system");
	Out.Description = TEXT("运行时写 ASC。action=activate_ability|cancel_ability|apply_effect|remove_effect|set_attribute。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),        FNexusSchema::Enum(TEXT("写操作"),
			{ TEXT("activate_ability"), TEXT("cancel_ability"), TEXT("apply_effect"), TEXT("remove_effect"), TEXT("set_attribute") }))
		.Prop(TEXT("actorName"),     FNexusSchema::Str(TEXT("Actor 名称（可选；省略取首个带 ASC 的 Pawn/Actor）")))
		.Prop(TEXT("abilityPath"),   FNexusSchema::Str(TEXT("GameplayAbility 资产路径（activate/cancel）")))
		.Prop(TEXT("effectPath"),    FNexusSchema::Str(TEXT("GameplayEffect 资产路径（apply/remove）")))
		.Prop(TEXT("attributeName"), FNexusSchema::Str(TEXT("属性名，格式 AttributeSetName.AttributeName（set_attribute）")))
		.Prop(TEXT("value"),         FNexusSchema::Num(TEXT("属性新基础值（set_attribute）")))
		.Prop(TEXT("level"),         FNexusSchema::Num(TEXT("Ability/Effect 等级（默认 1）"), 1.0))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("asc"), TEXT("ability"), TEXT("effect"), TEXT("attribute") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_ability_system"), TEXT("get_gameplay_tags") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("PIE 中施放/取消 Ability、应用/移除 GE、修改 Attribute 基础值");
}

// ── 执行 ──────────────────────────────────────────────────────────────────────

FCapabilityResult FInteractRuntimeActorAbilitySystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString Action;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}

		// 定位 World
		FString WorldError;
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(WorldError);
		if (!World) { OutError = WorldError; return; }

		// 定位 Actor
		FString ActorName;
		if (Arguments.IsValid()) Arguments->TryGetStringField(TEXT("actorName"), ActorName);

		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;

		if (ActorName.IsEmpty())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				ASC = FNexusGasUtils::FindAbilitySystemComponent(*It);
				if (ASC) { Actor = *It; break; }
			}
			if (!ASC) { OutError = TEXT("World 中未找到带 AbilitySystemComponent 的 Actor"); return; }
		}
		else
		{
			Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor) { OutError = FString::Printf(TEXT("Actor 未找到: %s"), *ActorName); return; }
			ASC = FNexusGasUtils::FindAbilitySystemComponent(Actor);
			if (!ASC) { OutError = FString::Printf(TEXT("Actor '%s' 无 AbilitySystemComponent"), *ActorName); return; }
		}

		// 提取公共参数
		FString AbilityPath, EffectPath, AttrName;
		double Level = 1.0, Value = 0.0;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("abilityPath"),   AbilityPath);
			Arguments->TryGetStringField(TEXT("effectPath"),    EffectPath);
			Arguments->TryGetStringField(TEXT("attributeName"), AttrName);
			Arguments->TryGetNumberField(TEXT("level"),  Level);
			Arguments->TryGetNumberField(TEXT("value"),  Value);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("actorName"), Actor->GetName());
		Entry->SetStringField(TEXT("action"),    Action);

		if (Action.Equals(TEXT("activate_ability"), ESearchCase::IgnoreCase))
		{
			if (AbilityPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("activate_ability 需要 abilityPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UGameplayAbility* AbilityCDO = LoadObject<UGameplayAbility>(nullptr, *AbilityPath);
			if (!AbilityCDO)
			{
				// 尝试按 Blueprint GeneratedClass 路径
				const FString CfgPath = AbilityPath + TEXT("_C");
				if (UClass* Cls = LoadObject<UClass>(nullptr, *CfgPath))
				{
					if (Cls->IsChildOf(UGameplayAbility::StaticClass()))
					{
						const FGameplayAbilitySpec Spec(Cls, static_cast<float>(Level));
						ASC->GiveAbility(Spec);
						const bool bActivated = ASC->TryActivateAbilityByClass(Cls, true);
						Entry->SetStringField(TEXT("abilityClass"), Cls->GetName());
						Entry->SetNumberField(TEXT("level"), Level);
						Entry->SetBoolField(TEXT("activated"), bActivated);
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						return;
					}
				}
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Ability 加载失败: %s"), *AbilityPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UClass* AbilityClass = AbilityCDO->GetClass();
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, static_cast<float>(Level)));
			const bool bActivated = ASC->TryActivateAbilityByClass(AbilityClass, true);
			Entry->SetStringField(TEXT("abilityClass"), AbilityClass->GetName());
			Entry->SetNumberField(TEXT("level"), Level);
			Entry->SetBoolField(TEXT("activated"), bActivated);
		}
		else if (Action.Equals(TEXT("cancel_ability"), ESearchCase::IgnoreCase))
		{
			if (AbilityPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("cancel_ability 需要 abilityPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UGameplayAbility* AbilityCDO = LoadObject<UGameplayAbility>(nullptr, *AbilityPath);
			if (!AbilityCDO)
			{
				const FString CfgPath = AbilityPath + TEXT("_C");
				if (UClass* Cls = LoadObject<UClass>(nullptr, *CfgPath))
				{
					if (Cls->IsChildOf(UGameplayAbility::StaticClass()))
					{
						ASC->CancelAbility(Cast<UGameplayAbility>(Cls->GetDefaultObject()));
						Entry->SetStringField(TEXT("abilityClass"), Cls->GetName());
						Entry->SetBoolField(TEXT("cancelled"), true);
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						return;
					}
				}
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Ability 加载失败: %s"), *AbilityPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			ASC->CancelAbility(AbilityCDO);
			Entry->SetStringField(TEXT("abilityClass"), AbilityCDO->GetClass()->GetName());
			Entry->SetBoolField(TEXT("cancelled"), true);
		}
		else if (Action.Equals(TEXT("apply_effect"), ESearchCase::IgnoreCase))
		{
			if (EffectPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("apply_effect 需要 effectPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UGameplayEffect* GECDO = LoadObject<UGameplayEffect>(nullptr, *EffectPath);
			if (!GECDO)
			{
				const FString CfgPath = EffectPath + TEXT("_C");
				if (UClass* Cls = LoadObject<UClass>(nullptr, *CfgPath))
				{
					if (Cls->IsChildOf(UGameplayEffect::StaticClass()))
					{
						GECDO = Cast<UGameplayEffect>(Cls->GetDefaultObject());
					}
				}
			}
			if (!GECDO)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("GameplayEffect 加载失败: %s"), *EffectPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(GECDO->GetClass(), static_cast<float>(Level), ASC->MakeEffectContext());
			if (!SpecHandle.IsValid())
			{
				Entry->SetStringField(TEXT("error"), TEXT("创建 GameplayEffectSpec 失败"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FActiveGameplayEffectHandle ActiveHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			Entry->SetStringField(TEXT("effectClass"), GECDO->GetClass()->GetName());
			Entry->SetNumberField(TEXT("level"), Level);
			Entry->SetBoolField(TEXT("applied"), ActiveHandle.IsValid());
		}
		else if (Action.Equals(TEXT("remove_effect"), ESearchCase::IgnoreCase))
		{
			if (EffectPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_effect 需要 effectPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			// 按类名匹配活跃 GE 并移除
			FString EffectClassName = FPaths::GetBaseFilename(EffectPath);
			int32 RemovedCount = 0;
			TArray<FActiveGameplayEffectHandle> AllHandles = ASC->GetActiveEffects(FGameplayEffectQuery());
			for (const FActiveGameplayEffectHandle& Handle : AllHandles)
			{
				if (!Handle.IsValid()) continue;
				const FActiveGameplayEffect* ActiveGE = ASC->GetActiveGameplayEffect(Handle);
				if (!ActiveGE || !ActiveGE->Spec.Def) continue;
				if (ActiveGE->Spec.Def->GetClass()->GetName().Contains(EffectClassName))
				{
					ASC->RemoveActiveGameplayEffect(Handle);
					++RemovedCount;
				}
			}
			Entry->SetStringField(TEXT("effectClass"), EffectClassName);
			Entry->SetNumberField(TEXT("removedCount"), RemovedCount);
		}
		else if (Action.Equals(TEXT("set_attribute"), ESearchCase::IgnoreCase))
		{
			if (AttrName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_attribute 需要 attributeName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			// attributeName 格式: AttributeSetName.AttributeName 或纯属性名
			FGameplayAttribute Attr;
			for (const UAttributeSet* AttrSet : ASC->GetSpawnedAttributes())
			{
				if (!AttrSet) continue;
				for (TFieldIterator<FProperty> PropIt(AttrSet->GetClass()); PropIt; ++PropIt)
				{
					FStructProperty* StructProp = CastField<FStructProperty>(*PropIt);
					if (!StructProp || !StructProp->Struct || !StructProp->Struct->IsChildOf(FGameplayAttributeData::StaticStruct())) continue;
					const FString FullName = FString::Printf(TEXT("%s.%s"), *AttrSet->GetClass()->GetName(), *PropIt->GetName());
					if (PropIt->GetName() == AttrName || FullName == AttrName)
					{
						Attr = FGameplayAttribute(StructProp);
						break;
					}
				}
				if (Attr.IsValid()) break;
			}
			if (!Attr.IsValid())
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Attribute 未找到: %s"), *AttrName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			const float OldBase = ASC->GetNumericAttributeBase(Attr);
			ASC->SetNumericAttributeBase(Attr, static_cast<float>(Value));
			Entry->SetStringField(TEXT("attributeName"), AttrName);
			Entry->SetNumberField(TEXT("oldBaseValue"), OldBase);
			Entry->SetNumberField(TEXT("newBaseValue"), static_cast<float>(Value));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorAbilitySystemCapability)

#endif // WITH_GAS
