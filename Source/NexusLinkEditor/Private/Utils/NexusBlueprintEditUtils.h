// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UBlueprint;
class UActorComponent;

/** 蓝图继承预检和组件模板解析，共用于读取、预览和写入。 */
struct FNexusBlueprintEditUtils final
{
	static bool ValidateLeaf(UBlueprint* Blueprint, FString& Error);
	static bool PreviewReparent(UBlueprint* Blueprint, const FString& ParentClass, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static bool Reparent(UBlueprint* Blueprint, const FString& ParentClass, bool bAllowPotentialDataLoss, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static bool ChangeVariableType(UBlueprint* Blueprint, const FString& Name, const FString& Type, bool bAllowPotentialDataLoss, bool bPreviewOnly, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static UActorComponent* FindComponent(UBlueprint* Blueprint, const FString& Name, bool bForWrite, FString& Error);
	static UActorComponent* FindComponentBaseline(UBlueprint* Blueprint, UActorComponent* Component, const FString& Name, const FString& Baseline, FString& Error);
};
