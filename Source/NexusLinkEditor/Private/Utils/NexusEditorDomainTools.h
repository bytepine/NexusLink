// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NexusEditorOperation.h"

/** 直接调用引擎领域 API，不经过官方工具注册表。 */
struct FNexusEditorDomainTools final
{
    static TSharedRef<FJsonObject> Reference(const FString& Path) { auto Ref = MakeShared<FJsonObject>(); Ref->SetStringField(TEXT("refPath"), Path); return Ref; }
    static bool Handles(const FString& Domain);
    static void Call(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
        const TSharedPtr<FNexusEditorOperation>& Operation);
    static bool PCG(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static void ExecutePCG(const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation);
    static bool Niagara(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static bool Animation(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static UObject* Resolve(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key);
    static bool SetProperties(UObject* Target, const TSharedPtr<FJsonObject>& Values, FString& Error);
};
