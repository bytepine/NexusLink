// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
class UWidgetBlueprint;
class FJsonObject;
struct FNexusWidgetDiagnostics final
{
	static void Read(UWidgetBlueprint* Blueprint, const FString& Section, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Result, FString& Error);
};
