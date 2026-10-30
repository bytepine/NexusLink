// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
struct FNexusAnimationAdvancedTools final
{
    static bool ValidateFileIO(const FString& Tool,const TSharedPtr<FJsonObject>& Args,FString& Error);
    static bool Handles(const FString& Domain,const FString& Tool);
    static bool Call(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Result,FString& Error);
};
