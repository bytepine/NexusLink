// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusGasUtils.h"

#if WITH_GAS

#include "Utils/NexusAssetUtils.h"
#include "Engine/Blueprint.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagsManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "UObject/UObjectIterator.h"

// ── 内部辅助 ──────────────────────────────────────────────────────────────────

/** 判断 Blueprint 生成类是否为目标类的子类（含自身）。 */
static bool IsBlueprintOfClass(UBlueprint* BP, UClass* RequiredParent)
{
	if (!BP || !RequiredParent) return false;
	UClass* Gen = BP->GeneratedClass;
	return Gen && Gen->IsChildOf(RequiredParent);
}

/** 通用 Blueprint 加载辅助：先按路径加载 UBlueprint，再校验父类链。 */
static UBlueprint* LoadAndValidateBP(const FString& AssetPath, UClass* RequiredParent, FString& OutError)
{
	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Blueprint 未找到: %s"), *AssetPath);
		return nullptr;
	}
	if (!IsBlueprintOfClass(BP, RequiredParent))
	{
		OutError = FString::Printf(
			TEXT("Blueprint %s 的生成类不是 %s 的子类"),
			*AssetPath, *RequiredParent->GetName());
		return nullptr;
	}
	return BP;
}

// ── 资产加载 ──────────────────────────────────────────────────────────────────

UBlueprint* FNexusGasUtils::LoadGameplayAbilityBlueprint(const FString& AssetPath, FString& OutError)
{
	return LoadAndValidateBP(AssetPath, UGameplayAbility::StaticClass(), OutError);
}

UBlueprint* FNexusGasUtils::LoadGameplayEffectBlueprint(const FString& AssetPath, FString& OutError)
{
	return LoadAndValidateBP(AssetPath, UGameplayEffect::StaticClass(), OutError);
}

UBlueprint* FNexusGasUtils::LoadAttributeSetBlueprint(const FString& AssetPath, FString& OutError)
{
	return LoadAndValidateBP(AssetPath, UAttributeSet::StaticClass(), OutError);
}

// ── Tag 容器 ──────────────────────────────────────────────────────────────────

TArray<TSharedPtr<FJsonValue>> FNexusGasUtils::SerializeTagContainer(const FGameplayTagContainer& Container)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FGameplayTag& Tag : Container)
	{
		Out.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	return Out;
}

bool FNexusGasUtils::ApplyTagContainer(
	FGameplayTagContainer& Container,
	const TArray<FString>& Tags,
	const FString&         Mode,
	FString&               OutError)
{
	UGameplayTagsManager& Mgr = UGameplayTagsManager::Get();

	FGameplayTagContainer NewTags;
	for (const FString& TagStr : Tags)
	{
		FGameplayTag Tag = Mgr.RequestGameplayTag(FName(*TagStr), false);
		if (!Tag.IsValid())
		{
			OutError = FString::Printf(TEXT("GameplayTag 无效: '%s'"), *TagStr);
			return false;
		}
		NewTags.AddTag(Tag);
	}

	if (Mode == TEXT("set"))
	{
		Container = NewTags;
	}
	else if (Mode == TEXT("add"))
	{
		Container.AppendTags(NewTags);
	}
	else if (Mode == TEXT("remove"))
	{
		Container.RemoveTags(NewTags);
	}
	else
	{
		OutError = FString::Printf(TEXT("无效的 mode '%s'（期望 set/add/remove）"), *Mode);
		return false;
	}
	return true;
}

// ── GE Modifier ───────────────────────────────────────────────────────────────

// GE->Modifiers 在 UE 4.26 是 protected，通过反射访问
static TArray<FGameplayModifierInfo>* GetGEModifiersPtr(UGameplayEffect* GE)
{
	FArrayProperty* Prop = FindFProperty<FArrayProperty>(GE->GetClass(), TEXT("Modifiers"));
	return Prop ? Prop->ContainerPtrToValuePtr<TArray<FGameplayModifierInfo>>(GE) : nullptr;
}
static const TArray<FGameplayModifierInfo>* GetGEModifiersConstPtr(const UGameplayEffect* GE)
{
	return GetGEModifiersPtr(const_cast<UGameplayEffect*>(GE));
}

TArray<TSharedPtr<FJsonValue>> FNexusGasUtils::SerializeGEModifiers(const UGameplayEffect* GE)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!GE) return Out;

	const TArray<FGameplayModifierInfo>* Mods = GetGEModifiersConstPtr(GE);
	if (!Mods) return Out;

	for (int32 i = 0; i < Mods->Num(); ++i)
	{
		const FGameplayModifierInfo& Mod = (*Mods)[i];
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), i);
		Obj->SetStringField(TEXT("attribute"), Mod.Attribute.IsValid() ? Mod.Attribute.GetName() : TEXT(""));

		// ModifierOp 枚举 → 字符串
		switch (Mod.ModifierOp)
		{
			case EGameplayModOp::Additive:    Obj->SetStringField(TEXT("modifierOp"), TEXT("Add"));       break;
			case EGameplayModOp::Multiplicitive: Obj->SetStringField(TEXT("modifierOp"), TEXT("Multiply")); break;
			case EGameplayModOp::Division:    Obj->SetStringField(TEXT("modifierOp"), TEXT("Divide"));    break;
			case EGameplayModOp::Override:    Obj->SetStringField(TEXT("modifierOp"), TEXT("Override"));  break;
			default:                          Obj->SetStringField(TEXT("modifierOp"), TEXT("Unknown"));   break;
		}

		// Magnitude：只读出标量（ScalableFloat）；Curve 型输出类型说明
		const FGameplayEffectModifierMagnitude& Mag = Mod.ModifierMagnitude;
		EGameplayEffectMagnitudeCalculation CalcType = Mag.GetMagnitudeCalculationType();
		switch (CalcType)
		{
			case EGameplayEffectMagnitudeCalculation::ScalableFloat:
			{
				float Val = 0.f;
				Mag.GetStaticMagnitudeIfPossible(1.f, Val);
				Obj->SetStringField(TEXT("magnitudeType"), TEXT("ScalableFloat"));
				Obj->SetNumberField(TEXT("magnitude"), Val);
				break;
			}
			case EGameplayEffectMagnitudeCalculation::AttributeBased:
				Obj->SetStringField(TEXT("magnitudeType"), TEXT("AttributeBased"));
				break;
			case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:
				Obj->SetStringField(TEXT("magnitudeType"), TEXT("CustomCalculationClass"));
				break;
			case EGameplayEffectMagnitudeCalculation::SetByCaller:
				Obj->SetStringField(TEXT("magnitudeType"), TEXT("SetByCaller"));
				break;
			default:
				Obj->SetStringField(TEXT("magnitudeType"), TEXT("Unknown"));
				break;
		}

		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}
	return Out;
}

bool FNexusGasUtils::ApplyGEModifierOp(
	UGameplayEffect*               GE,
	const FString&                 Action,
	const TSharedPtr<FJsonObject>& OpArgs,
	FString&                       OutError)
{
	if (!GE || !OpArgs.IsValid()) { OutError = TEXT("GE 或操作参数无效"); return false; }

	if (Action == TEXT("add_modifier"))
	{
		FString AttrName, OpStr;
		float Magnitude = 0.f;
		if (!OpArgs->TryGetStringField(TEXT("attribute"), AttrName) || AttrName.IsEmpty())
		{ OutError = TEXT("add_modifier 需要 attribute"); return false; }
		if (!OpArgs->TryGetStringField(TEXT("modifierOp"), OpStr) || OpStr.IsEmpty())
		{ OutError = TEXT("add_modifier 需要 modifierOp（Add/Multiply/Divide/Override）"); return false; }
		if (OpArgs->HasField(TEXT("magnitude")))
			Magnitude = (float)OpArgs->GetNumberField(TEXT("magnitude"));

		// 查找 Attribute（不传 flags，默认包含父类属性，兼容 UE4/UE5）
		FGameplayAttribute Attr;
		for (TFieldIterator<FProperty> PropIt(UAttributeSet::StaticClass()); PropIt; ++PropIt)
		{
			if (PropIt->GetName().Equals(AttrName, ESearchCase::IgnoreCase))
			{
				Attr = FGameplayAttribute(*PropIt);
				break;
			}
		}
		// 若在基类找不到，按名直接构造（对应项目自定义 AttributeSet）
		if (!Attr.IsValid())
		{
			// 遍历所有已加载 AttributeSet 子类寻找匹配属性
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (!It->IsChildOf(UAttributeSet::StaticClass()) || *It == UAttributeSet::StaticClass()) continue;
				for (TFieldIterator<FProperty> PropIt(*It); PropIt; ++PropIt)
				{
					if (PropIt->GetName().Equals(AttrName, ESearchCase::IgnoreCase))
					{
						Attr = FGameplayAttribute(*PropIt);
						break;
					}
				}
				if (Attr.IsValid()) break;
			}
		}
		if (!Attr.IsValid())
		{
			OutError = FString::Printf(TEXT("未找到 GameplayAttribute: %s"), *AttrName);
			return false;
		}

		EGameplayModOp::Type ModOp = EGameplayModOp::Additive;
		if      (OpStr == TEXT("Multiply")) ModOp = EGameplayModOp::Multiplicitive;
		else if (OpStr == TEXT("Divide"))   ModOp = EGameplayModOp::Division;
		else if (OpStr == TEXT("Override")) ModOp = EGameplayModOp::Override;

		TArray<FGameplayModifierInfo>* Mods = GetGEModifiersPtr(GE);
		if (!Mods) { OutError = TEXT("无法访问 GE Modifiers 数组"); return false; }

		FGameplayModifierInfo NewMod;
		NewMod.Attribute = Attr;
		NewMod.ModifierOp = ModOp;
		FScalableFloat SF(Magnitude);
		NewMod.ModifierMagnitude = FGameplayEffectModifierMagnitude(SF);
		Mods->Add(NewMod);
		return true;
	}

	if (Action == TEXT("remove_modifier"))
	{
		TArray<FGameplayModifierInfo>* Mods = GetGEModifiersPtr(GE);
		if (!Mods) { OutError = TEXT("无法访问 GE Modifiers 数组"); return false; }

		int32 Index = -1;
		if (!OpArgs->HasField(TEXT("index"))) { OutError = TEXT("remove_modifier 需要 index"); return false; }
		Index = FMath::RoundToInt((float)OpArgs->GetNumberField(TEXT("index")));
		if (!Mods->IsValidIndex(Index))
		{ OutError = FString::Printf(TEXT("Modifier index %d 超出范围 [0, %d)"), Index, Mods->Num()); return false; }
		Mods->RemoveAt(Index);
		return true;
	}

	if (Action == TEXT("set_modifier"))
	{
		TArray<FGameplayModifierInfo>* Mods = GetGEModifiersPtr(GE);
		if (!Mods) { OutError = TEXT("无法访问 GE Modifiers 数组"); return false; }

		int32 Index = -1;
		if (!OpArgs->HasField(TEXT("index"))) { OutError = TEXT("set_modifier 需要 index"); return false; }
		Index = FMath::RoundToInt((float)OpArgs->GetNumberField(TEXT("index")));
		if (!Mods->IsValidIndex(Index))
		{ OutError = FString::Printf(TEXT("Modifier index %d 超出范围 [0, %d)"), Index, Mods->Num()); return false; }

		if (OpArgs->HasField(TEXT("magnitude")))
		{
			float Magnitude = (float)OpArgs->GetNumberField(TEXT("magnitude"));
			FScalableFloat SF(Magnitude);
			(*Mods)[Index].ModifierMagnitude = FGameplayEffectModifierMagnitude(SF);
		}
		return true;
	}

	OutError = FString::Printf(TEXT("未知 Modifier 操作: %s"), *Action);
	return false;
}

// ── AttributeSet 属性序列化 ───────────────────────────────────────────────────

TArray<TSharedPtr<FJsonValue>> FNexusGasUtils::SerializeGameplayAttributes(
	UClass* AttributeSetClass, UObject* CDO)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!AttributeSetClass || !CDO) return Out;

	for (TFieldIterator<FProperty> PropIt(AttributeSetClass); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

		// 只导出 FGameplayAttributeData 或其子类型属性
		FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (!StructProp) continue;
		if (!StructProp->Struct || !StructProp->Struct->IsChildOf(FGameplayAttributeData::StaticStruct())) continue;

		const FGameplayAttributeData* AttrData =
			StructProp->ContainerPtrToValuePtr<FGameplayAttributeData>(CDO);
		if (!AttrData) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"),         Prop->GetName());
		Obj->SetNumberField(TEXT("baseValue"),     AttrData->GetBaseValue());
		Obj->SetNumberField(TEXT("currentValue"),  AttrData->GetCurrentValue());
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}
	return Out;
}

// ── 运行时 ASC 查找 ───────────────────────────────────────────────────────────

UAbilitySystemComponent* FNexusGasUtils::FindAbilitySystemComponent(AActor* Actor)
{
	if (!Actor) return nullptr;

	// 1. Actor 本身直接带 ASC 接口
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			return ASC;
	}

	// 2. Pawn 的 PlayerState 上挂载 ASC（GAS 常见模式）
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (APlayerState* PS = Pawn->GetPlayerState())
		{
			if (IAbilitySystemInterface* PSASI = Cast<IAbilitySystemInterface>(PS))
			{
				if (UAbilitySystemComponent* ASC = PSASI->GetAbilitySystemComponent())
					return ASC;
			}
		}
	}

	// 3. 在 Actor 的所有组件中搜索
	return Actor->FindComponentByClass<UAbilitySystemComponent>();
}

#endif // WITH_GAS
