// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "NexusEditorOperation.h"

/** 引擎自带工具的受控入口，不接受客户端提供的任意 Toolset/Tool 名。 */
struct FNexusEditorToolsets final
{
	static TSharedPtr<FNexusEditorOperation> Slate(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	static TSharedPtr<FNexusEditorOperation> Automation(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	static TSharedPtr<FNexusEditorOperation> Compile();
	static TArray<TSharedPtr<FJsonValue>> WindowRefs();
    static FString BindNativeSnapshot(const FString& Snapshot, bool& bTruncated);
	static void AddTicker(const FTickerDelegate& Delegate, float Interval);
	static void ShutdownLiveCoding();
	static void Shutdown();
};

struct FNexusNativeEditorTools final
{
	static bool UseNative(const FString& Domain);
    static TSharedPtr<FJsonObject> Describe(const FString& Capability);
    static bool CanCall(const FString& Domain, const FString& Tool);
    static TSharedPtr<FNexusEditorOperation> Scratch() {
        auto Operation = MakeShared<FNexusEditorOperation>(); Operation->Backend = TEXT("OfficialToolset");
        Operation->StartedAt = FPlatformTime::Seconds(); return Operation;
    }
	static void Call(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FNexusEditorOperation>& Operation);
	static void Shutdown();
};
