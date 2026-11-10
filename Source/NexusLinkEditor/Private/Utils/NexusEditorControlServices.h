// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "NexusEditorOperation.h"

/** NexusLink 自有编辑器服务，直接调用 Slate、Automation 与 Live Coding API。 */
struct FNexusEditorControlServices final
{
	static TSharedPtr<FNexusEditorOperation> Slate(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	static TSharedPtr<FNexusEditorOperation> Automation(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	static TSharedPtr<FNexusEditorOperation> Compile();
	static TArray<TSharedPtr<FJsonValue>> WindowRefs();
	static void AddTicker(const FTickerDelegate& Delegate, float Interval);
	static void ShutdownLiveCoding();
	static void Shutdown();
};
