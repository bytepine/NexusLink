// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/GAS/NexusInteractRuntimeActorAbilitySystemCapability.h"

#if WITH_GAS

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusGasDiagnostics.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayCueInterface.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "NexusMcpTool.h"

// ── Capability 元数据 ─────────────────────────────────────────────────────────

void FNexusInteractRuntimeActorAbilitySystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeActorAbilitySystem");
	Out.Description = TEXT("Command ASC. Try existing abilities, capture activation/commit/failure events; grant/apply/attributes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),        FNexusSchema::Enum(TEXT("Write operation"),
			{ TEXT("ActivateAbility"), TEXT("CancelAbility"), TEXT("ApplyEffect"), TEXT("RemoveEffect"), TEXT("SetAttribute"),
			  TEXT("GiveAbility"), TEXT("ClearAbility"), TEXT("ExecuteCue"), TEXT("AddLooseTag"), TEXT("RemoveLooseTag"),
			  TEXT("TryActivateGrantedAbility"), TEXT("StartActivationCapture"), TEXT("StopActivationCapture") }))
		.Prop(TEXT("ActorName"),     FNexusSchema::Str(TEXT("Actor name (optional; first ASC Pawn/Actor if omitted)")))
		.Prop(TEXT("AbilityPath"),   FNexusSchema::Str(TEXT("GameplayAbility asset path")))
		.Prop(TEXT("SpecHandle"), FNexusSchema::Str(TEXT("Granted ability handle; exclusive with AbilityPath for TryActivateGrantedAbility")))
		.Prop(TEXT("CaptureId"), FNexusSchema::Str(TEXT("Capture to stop; bound to ActorName")))
		.Prop(TEXT("EffectPath"),    FNexusSchema::Str(TEXT("GameplayEffect asset path (apply/remove)")))
		.Prop(TEXT("AttributeName"), FNexusSchema::Str(TEXT("Attribute name: AttributeSetName.AttributeName (SetAttribute)")))
		.Prop(TEXT("Value"),         FNexusSchema::Num(TEXT("New attribute base value (SetAttribute)")))
		.Prop(TEXT("Level"),         FNexusSchema::Num(TEXT("Ability/Effect level (default 1)"), 1.0))
		.Prop(TEXT("Tag"),           FNexusSchema::Str(TEXT("GameplayTag（ExecuteCue / loose tag）")))
		.Required({ TEXT("Action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("asc"), TEXT("ability"), TEXT("effect"), TEXT("Attribute"), TEXT("activation"), TEXT("failure"), TEXT("prediction"), TEXT("capture") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorAbilitySystem"), TEXT("GetGameplayTags") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Reproduce GAS activation failures");
}

// ── 执行 ──────────────────────────────────────────────────────────────────────

FCapabilityResult FNexusInteractRuntimeActorAbilitySystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("Action"));

		// 定位 World
		FString WorldError;
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(WorldError);
		if (!World) { OutError = WorldError; return; }

		// 定位 Actor
		FString ActorName;
		if (Arguments.IsValid()) Arguments->TryGetStringField(TEXT("ActorName"), ActorName);
		const bool bDiagnosticAction = Action == TEXT("TryActivateGrantedAbility")
			|| Action == TEXT("StartActivationCapture") || Action == TEXT("StopActivationCapture");
		if (bDiagnosticAction && ActorName.IsEmpty())
		{
			TSharedPtr<FJsonObject> ErrorEntry = MakeShared<FJsonObject>();
			ErrorEntry->SetStringField(TEXT("Error"), TEXT("Diagnostic actions require explicit ActorName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(ErrorEntry));
			return;
		}

		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;

		if (ActorName.IsEmpty())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				ASC = FNexusGasUtils::FindAbilitySystemComponent(*It);
				if (ASC) { Actor = *It; break; }
			}
			if (!ASC) { OutError = TEXT("No Actor with AbilitySystemComponent found in World"); return; }
		}
		else
		{
			Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return; }
			ASC = FNexusGasUtils::FindAbilitySystemComponent(Actor);
			if (!ASC) { OutError = FString::Printf(TEXT("Actor '%s' has no AbilitySystemComponent"), *ActorName); return; }
		}

		// 提取公共参数
		FString AbilityPath, EffectPath, AttrName, TagStr;
		double Level = 1.0, Value = 0.0;
		if (Arguments.IsValid())
		{
			AbilityPath = FNexusArgs(Arguments).Str(TEXT("AbilityPath"), AbilityPath);
			EffectPath = FNexusArgs(Arguments).Str(TEXT("EffectPath"), EffectPath);
			AttrName = FNexusArgs(Arguments).Str(TEXT("AttributeName"), AttrName);
			TagStr = FNexusArgs(Arguments).Str(TEXT("Tag"), TagStr);
			Arguments->TryGetNumberField(TEXT("Level"),  Level);
			Arguments->TryGetNumberField(TEXT("Value"),  Value);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
		Entry->SetStringField(TEXT("Action"),    Action);

		if (bDiagnosticAction)
		{
			FString DiagnosticError;
			if (Action == TEXT("StartActivationCapture"))
				Entry->SetStringField(TEXT("CaptureId"), FNexusGasDiagnostics::StartCapture(ASC, DiagnosticError));
			else if (Action == TEXT("StopActivationCapture"))
				Entry->SetBoolField(TEXT("bStopped"), FNexusGasDiagnostics::StopCapture(ASC, A.Str(TEXT("CaptureId")), DiagnosticError));
			else
			{
				FGameplayAbilitySpec* Spec = FNexusGasDiagnostics::FindSpec(ASC, Arguments, DiagnosticError);
				if (!Spec && DiagnosticError.IsEmpty()) DiagnosticError = TEXT("Ability not granted or not visible on this endpoint");
				if (Spec)
				{
					// 激活可能清除或修改 Spec；调用前复制句柄，之后不再解引用它。
					const FGameplayAbilitySpecHandle Handle = Spec->Handle;
					Entry->SetStringField(TEXT("SpecHandle"), Handle.ToString());
					Entry->SetBoolField(TEXT("bRequestAccepted"), ASC->TryActivateAbility(Handle, true));
					Entry->SetStringField(TEXT("AuthorityOutcome"), TEXT("Unconfirmed; inspect authority capture"));
				}
			}
			if (!DiagnosticError.IsEmpty()) Entry->SetStringField(TEXT("Error"), DiagnosticError);
		}
		else if (Action.Equals(TEXT("ActivateAbility"), ESearchCase::IgnoreCase))
		{
			if (AbilityPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("ActivateAbility requires AbilityPath"));
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
						Entry->SetStringField(TEXT("AbilityClass"), Cls->GetName());
						Entry->SetNumberField(TEXT("Level"), Level);
						Entry->SetBoolField(TEXT("bActivated"), bActivated);
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						return;
					}
				}
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load Ability: %s"), *AbilityPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UClass* AbilityClass = AbilityCDO->GetClass();
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, static_cast<float>(Level)));
			const bool bActivated = ASC->TryActivateAbilityByClass(AbilityClass, true);
			Entry->SetStringField(TEXT("AbilityClass"), AbilityClass->GetName());
			Entry->SetNumberField(TEXT("Level"), Level);
			Entry->SetBoolField(TEXT("bActivated"), bActivated);
		}
		else if (Action.Equals(TEXT("CancelAbility"), ESearchCase::IgnoreCase))
		{
			if (AbilityPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("CancelAbility requires AbilityPath"));
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
						Entry->SetStringField(TEXT("AbilityClass"), Cls->GetName());
						Entry->SetBoolField(TEXT("bCancelled"), true);
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						return;
					}
				}
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load Ability: %s"), *AbilityPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			ASC->CancelAbility(AbilityCDO);
			Entry->SetStringField(TEXT("AbilityClass"), AbilityCDO->GetClass()->GetName());
			Entry->SetBoolField(TEXT("bCancelled"), true);
		}
		else if (Action.Equals(TEXT("ApplyEffect"), ESearchCase::IgnoreCase))
		{
			if (EffectPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("ApplyEffect requires EffectPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UGameplayEffect* GECDO = LoadObject<UGameplayEffect>(nullptr, *EffectPath);
			if (!GECDO)
			{
				UClass* EffectClass = LoadObject<UClass>(nullptr, *EffectPath);
				if (!EffectClass) EffectClass = LoadObject<UClass>(nullptr, *(EffectPath + TEXT("_C")));
				if (UClass* Cls = EffectClass)
				{
					if (Cls->IsChildOf(UGameplayEffect::StaticClass()))
					{
						GECDO = Cast<UGameplayEffect>(Cls->GetDefaultObject());
					}
				}
			}
			if (!GECDO)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load GameplayEffect: %s"), *EffectPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(GECDO->GetClass(), static_cast<float>(Level), ASC->MakeEffectContext());
			if (!SpecHandle.IsValid())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Create GameplayEffectSpec failed"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FActiveGameplayEffectHandle ActiveHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			Entry->SetStringField(TEXT("EffectClass"), GECDO->GetClass()->GetName());
			Entry->SetNumberField(TEXT("Level"), Level);
			Entry->SetBoolField(TEXT("bApplied"), ActiveHandle.IsValid());
		}
		else if (Action.Equals(TEXT("RemoveEffect"), ESearchCase::IgnoreCase))
		{
			if (EffectPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("RemoveEffect requires EffectPath"));
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
			Entry->SetStringField(TEXT("EffectClass"), EffectClassName);
			Entry->SetNumberField(TEXT("RemovedCount"), RemovedCount);
		}
		else if (Action.Equals(TEXT("SetAttribute"), ESearchCase::IgnoreCase))
		{
			if (AttrName.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("SetAttribute requires AttributeName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			// AttributeName 格式: AttributeSetName.AttributeName 或纯属性名
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
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Attribute not found: %s"), *AttrName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			const float OldBase = ASC->GetNumericAttributeBase(Attr);
			ASC->SetNumericAttributeBase(Attr, static_cast<float>(Value));
			Entry->SetStringField(TEXT("AttributeName"), AttrName);
			Entry->SetNumberField(TEXT("OldBaseValue"), OldBase);
			Entry->SetNumberField(TEXT("NewBaseValue"), static_cast<float>(Value));
		}
		else if (Action.Equals(TEXT("GiveAbility"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("ClearAbility"), ESearchCase::IgnoreCase))
		{
			if (AbilityPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("AbilityPath required"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UClass* AbilityClass = nullptr;
			if (UGameplayAbility* CDO = LoadObject<UGameplayAbility>(nullptr, *AbilityPath))
			{
				AbilityClass = CDO->GetClass();
			}
			else if (UClass* Cls = LoadObject<UClass>(nullptr, *(AbilityPath + TEXT("_C"))))
			{
				if (Cls->IsChildOf(UGameplayAbility::StaticClass())) AbilityClass = Cls;
			}
			if (!AbilityClass)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load Ability: %s"), *AbilityPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (Action.Equals(TEXT("GiveAbility"), ESearchCase::IgnoreCase))
			{
				ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, static_cast<float>(Level)));
				Entry->SetBoolField(TEXT("bGiven"), true);
			}
			else
			{
				const TArray<FGameplayAbilitySpec>& Specs = ASC->GetActivatableAbilities();
				for (const FGameplayAbilitySpec& Spec : Specs)
				{
					if (Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
					{
						ASC->ClearAbility(Spec.Handle);
					}
				}
				Entry->SetBoolField(TEXT("bCleared"), true);
			}
			Entry->SetStringField(TEXT("AbilityClass"), AbilityClass->GetName());
		}
		else if (Action.Equals(TEXT("ExecuteCue"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("AddLooseTag"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("RemoveLooseTag"), ESearchCase::IgnoreCase))
		{
			if (TagStr.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("tag required"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
			if (!Tag.IsValid())
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid GameplayTag: %s"), *TagStr));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (Action.Equals(TEXT("ExecuteCue"), ESearchCase::IgnoreCase))
			{
				ASC->ExecuteGameplayCue(Tag);
				Entry->SetBoolField(TEXT("bExecuted"), true);
			}
			else if (Action.Equals(TEXT("AddLooseTag"), ESearchCase::IgnoreCase))
			{
				ASC->AddLooseGameplayTag(Tag);
				Entry->SetBoolField(TEXT("bAdded"), true);
			}
			else
			{
				ASC->RemoveLooseGameplayTag(Tag);
				Entry->SetBoolField(TEXT("bRemoved"), true);
			}
			Entry->SetStringField(TEXT("Tag"), TagStr);
		}
		else
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorAbilitySystemCapability)

#endif // WITH_GAS
