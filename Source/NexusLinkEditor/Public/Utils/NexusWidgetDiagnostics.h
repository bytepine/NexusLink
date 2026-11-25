// Copyright byteyang. All Rights Reserved.
#pragma once

class UWidgetBlueprint;
#include "CoreMinimal.h"
class UWidgetBlueprint;
class FJsonObject;
struct NEXUSLINKEDITOR_API FNexusWidgetDiagnostics final
{
    using FAppendBinding = TFunctionRef<void(const FString&, const FString&, const FString&, const FString&, const TSharedPtr<FJsonObject>&)>;
    using FMVVMBindings = TFunction<void(UWidgetBlueprint*, FAppendBinding)>;
    static FMVVMBindings GatherMVVMBindings;

	static void Read(UWidgetBlueprint* Blueprint, const FString& Section, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Result, FString& Error);
};
