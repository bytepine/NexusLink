// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NexusActionCapability.h"
class UBlueprint;
struct FNexusBlueprintGraphEnhancements final
{
    static void Write(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Context);
    static void Read(UBlueprint* Blueprint, const FString& Section, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
};
