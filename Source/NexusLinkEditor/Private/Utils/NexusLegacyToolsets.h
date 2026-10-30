// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NexusEditorOperation.h"

/** 仅在宿主未选择官方后端时执行固定的兼容动作。 */
struct FNexusLegacyToolsets final
{
    static bool Handles(const FString& Domain);
    static void Call(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
        const TSharedPtr<FNexusEditorOperation>& Operation);
    static bool PCG(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static bool Niagara(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static bool Animation(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static UObject* Resolve(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key);
    static bool SetProperties(UObject* Target, const TSharedPtr<FJsonObject>& Values, FString& Error);
};
