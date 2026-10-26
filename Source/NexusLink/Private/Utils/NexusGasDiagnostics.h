// Copyright byteyang. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#if WITH_GAS
class UAbilitySystemComponent;
struct FGameplayAbilitySpec;

/** GAS 诊断只读取状态；事件捕获单独订阅，不调用项目的激活检查。 */
struct FNexusGasDiagnostics final
{
	static FGameplayAbilitySpec* FindSpec(UAbilitySystemComponent* Asc, const TSharedPtr<FJsonObject>& Args, FString& Error);
	static TSharedPtr<FJsonObject> Context(UAbilitySystemComponent* Asc);
	static void Diagnose(UAbilitySystemComponent* Asc, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Detail, FString& Error);
	static FString StartCapture(UAbilitySystemComponent* Asc, FString& Error);
	static bool ReadCapture(UAbilitySystemComponent* Asc, const FString& Id, TSharedPtr<FJsonObject>& Detail, FString& Error);
	static bool StopCapture(UAbilitySystemComponent* Asc, const FString& Id, FString& Error);
	static void Shutdown();
};
#endif
