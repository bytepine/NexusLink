// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** 动作发现只描述固定路由，不执行或启用插件。 */
struct FNexusEditorActionSchema final
{
	static TArray<TSharedPtr<FJsonValue>> List(const FString& Capability, const FString& Action);
};
