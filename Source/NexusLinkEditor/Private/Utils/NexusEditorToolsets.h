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
	static TSharedRef<FJsonObject> Reference(const FString& Path) { auto Ref = MakeShared<FJsonObject>(); Ref->SetStringField(TEXT("refPath"), Path); return Ref; }
	static bool UseNative(const FString& Domain);
    static TSharedPtr<FJsonObject> Describe(const FString& Capability);
    static bool CanCall(const FString& Domain, const FString& Tool);
    static TSharedPtr<FNexusEditorOperation> Scratch() {
        auto Operation = MakeShared<FNexusEditorOperation>(); Operation->Backend = TEXT("OfficialToolset");
        Operation->StartedAt = FPlatformTime::Seconds(); return Operation;
    }
    // true 表示已选择原生后端（含失败），调用方不得再执行兼容写入。
    static bool EditSync(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
        const TSharedPtr<FJsonObject>& Entry) {
        if (!UseNative(Domain)) return false;
        TSharedPtr<FJsonValue> Value; FString Error;
        if (!InvokeSync(Domain, Tool, Args, Value, Error) || (Value.IsValid() && Value->Type == EJson::Boolean && !Value->AsBool()))
            Entry->SetStringField(TEXT("Error"), Error.IsEmpty() ? TEXT("Official tool reported false") : Error);
        else Entry->SetStringField(TEXT("Backend"), TEXT("OfficialToolset"));
        return true;
    }
    // 仅供白名单内的同步原生方法使用；不等待或泵送游戏线程。
    static bool InvokeSync(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
        TSharedPtr<FJsonValue>& Value, FString& Error);
	static void Call(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FNexusEditorOperation>& Operation);
	static void Shutdown();
};
