// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UWidgetBlueprint;
struct FNexusWidgetTreeCompat final
{
    static bool Apply(UWidgetBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Entry, FString& Error);
};
