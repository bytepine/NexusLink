// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UBlueprint;
class UActorComponent;
class AActor;
class FProperty;

/** 蓝图继承预检和组件模板解析，共用于读取、预览和写入。 */
struct FNexusBlueprintEditUtils final
{
	static bool ValidateLeaf(UBlueprint* Blueprint, FString& Error);
	static bool PreviewReparent(UBlueprint* Blueprint, const FString& ParentClass, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static bool Reparent(UBlueprint* Blueprint, const FString& ParentClass, bool bAllowPotentialDataLoss, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static bool RemapMemberReferences(UBlueprint* Blueprint, const FString& OldName, const FString& NewName, bool bValidateOnly, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static bool ChangeVariableType(UBlueprint* Blueprint, const FString& Name, const FString& Type, bool bAllowPotentialDataLoss, bool bPreviewOnly, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static UActorComponent* FindComponent(UBlueprint* Blueprint, const FString& Name, bool bForWrite, FString& Error, bool* bCreatedOverride = nullptr);
	static bool ResolveOwnedComponentProperty(UActorComponent* Component, const TArray<FString>& Segments, FProperty*& Property, void*& Value, UObject*& Owner, FString& Error);
	static void DiscardFailedComponentOverride(UBlueprint* Blueprint, UActorComponent* CreatedOverride, bool bWasDirty);
	static bool SetComponentProperty(UBlueprint* Blueprint, const FString& Name, const FString& Path, const FString& Text, FString& Previous, FString& Actual, FString& Error);
	static UActorComponent* FindComponentBaseline(UBlueprint* Blueprint, UActorComponent* Component, const FString& Name, const FString& Baseline, FString& Error);
	/** SCS 成员在 Actor CDO 上可能为空；按实际蓝图继承覆盖解析组件默认值。 */
	static bool ResolveActorDefaultProperty(AActor* Actor, const TArray<FString>& Segments, FProperty*& Property, void*& Value, FString& Error);
};
