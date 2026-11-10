// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NexusEditorOperation.h"

/** 直接使用 NiagaraEditor 的领域 API，不经过外部工具注册或分发。 */
struct FNexusNiagaraEditorTools final
{
    static void Call(const FString& Tool, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation);
    static bool Execute(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error);
    static TSharedPtr<FJsonObject> InputSchema(const FString& Tool);
};
