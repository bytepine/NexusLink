// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：GAS（GameplayAbility System）
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_GAS

#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemComponent.h"

class UGameplayAbility;
class UAttributeSet;

/**
 * GAS 资产与运行时的共享序列化工具。
 * 仅在 WITH_GAS=1 时编译，所有方法均为 static。
 */
class NEXUSLINK_API FNexusGasUtils final
{
public:
	FNexusGasUtils() = delete;

	// ── 资产加载 ────────────────────────────────────────────────────────────────

	/** 加载 GameplayAbility Blueprint；校验父类链，返回 nullptr 则 OutError 描述原因。 */
	static UBlueprint* LoadGameplayAbilityBlueprint(const FString& AssetPath, FString& OutError);

	/** 加载 GameplayEffect Blueprint；校验父类链。 */
	static UBlueprint* LoadGameplayEffectBlueprint(const FString& AssetPath, FString& OutError);

	/** 加载 AttributeSet Blueprint；校验父类链。 */
	static UBlueprint* LoadAttributeSetBlueprint(const FString& AssetPath, FString& OutError);

	// ── GameplayTag 容器序列化 ────────────────────────────────────────────────

	/** 将 FGameplayTagContainer 序列化为 Tag 字符串数组 JSON。 */
	static TArray<TSharedPtr<FJsonValue>> SerializeTagContainer(const FGameplayTagContainer& Container);

	/**
	 * 将 tags[] + mode 应用到 FGameplayTagContainer。
	 * mode: "set"（替换全部）/ "add"（追加）/ "remove"（移除）。
	 * 返回 false 并填 OutError 表示输入无效。
	 */
	static bool ApplyTagContainer(
		FGameplayTagContainer&        Container,
		const TArray<FString>&        Tags,
		const FString&                Mode,
		FString&                      OutError);

	// ── GE Modifier 序列化 ───────────────────────────────────────────────────

	/**
	 * 将 UGameplayEffect CDO 的 Modifiers 数组序列化为 JSON 数组。
	 * 每项含 attribute / modifierOp / magnitudeType / magnitude（标量值或 "curve"）。
	 */
	static TArray<TSharedPtr<FJsonValue>> SerializeGEModifiers(const UGameplayEffect* GE);

	/**
	 * 对 GE CDO 的 Modifiers 执行单次操作。
	 * action: "add_modifier" / "remove_modifier" / "set_modifier"（按 index）。
	 * 返回 false 并填 OutError 表示操作无效。
	 */
	static bool ApplyGEModifierOp(
		UGameplayEffect*                  GE,
		const FString&                    Action,
		const TSharedPtr<FJsonObject>&    OpArgs,
		FString&                          OutError);

	// ── AttributeSet 序列化 ─────────────────────────────────────────────────

	/**
	 * 将 AttributeSet CDO 的全部 FGameplayAttributeData 属性序列化为 JSON 数组。
	 * 每项含 name / baseValue / currentValue。
	 */
	static TArray<TSharedPtr<FJsonValue>> SerializeGameplayAttributes(
		UClass* AttributeSetClass, UObject* CDO);

	// ── 运行时 ASC 查找 ─────────────────────────────────────────────────────

	/**
	 * 在 Actor 上查找 AbilitySystemComponent。
	 * 常见挂载点：Actor 直接组件 → Pawn 的 PlayerState。
	 * 未找到返回 nullptr。
	 */
	static UAbilitySystemComponent* FindAbilitySystemComponent(AActor* Actor);
};

// ── 跨文件共享反射辅助（inline，避免 Unity Build 多定义错误）───────────────────
//
// 通过 UPROPERTY 反射读写 protected/private 成员，UE4/UE5 通用。
// 须在 WITH_GAS 块内使用（依赖 GameplayEffect.h / Abilities/GameplayAbility.h）。

/** 读取任意 UPROPERTY（无视 C++ 访问控制）。失败返回 nullptr。 */
template<typename T>
FORCEINLINE T* NxGasPropPtr(UObject* Obj, const TCHAR* PropName)
{
	FProperty* P = FindFProperty<FProperty>(Obj->GetClass(), PropName);
	return P ? P->ContainerPtrToValuePtr<T>(Obj) : nullptr;
}

/** 读取枚举底层 uint8（兼容 FByteProperty / FEnumProperty）。属性不存在返回 0。 */
FORCEINLINE uint8 NxGasGetEnumByte(UObject* CDO, const TCHAR* PropName)
{
	FProperty* P = FindFProperty<FProperty>(CDO->GetClass(), PropName);
	if (!P) return 0;
	if (FByteProperty* BP = CastField<FByteProperty>(P))
		return *BP->ContainerPtrToValuePtr<uint8>(CDO);
	if (FEnumProperty* EP = CastField<FEnumProperty>(P))
		return (uint8)EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(CDO));
	return 0;
}

/** 写入枚举底层 uint8（兼容 FByteProperty / FEnumProperty）。返回 false 表示属性不存在。 */
FORCEINLINE bool NxGasSetEnumByte(UObject* CDO, const TCHAR* PropName, uint8 Val)
{
	FProperty* P = FindFProperty<FProperty>(CDO->GetClass(), PropName);
	if (!P) return false;
	if (FByteProperty* BP = CastField<FByteProperty>(P))
		{ *BP->ContainerPtrToValuePtr<uint8>(CDO) = Val; return true; }
	if (FEnumProperty* EP = CastField<FEnumProperty>(P))
		{ EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(CDO), (int64)Val); return true; }
	return false;
}

/** 读取 TSubclassOf<T> UPROPERTY 的 UClass*。 */
FORCEINLINE UClass* NxGasGetClassProp(UObject* CDO, const TCHAR* PropName)
{
	FClassProperty* P = FindFProperty<FClassProperty>(CDO->GetClass(), PropName);
	return P ? Cast<UClass>(P->GetPropertyValue_InContainer(CDO)) : nullptr;
}

/** 写入 TSubclassOf<T> UPROPERTY 的 UClass*。 */
FORCEINLINE bool NxGasSetClassProp(UObject* CDO, const TCHAR* PropName, UClass* Value)
{
	FClassProperty* P = FindFProperty<FClassProperty>(CDO->GetClass(), PropName);
	if (!P) return false;
	P->SetPropertyValue_InContainer(CDO, Value);
	return true;
}

#endif // WITH_GAS
