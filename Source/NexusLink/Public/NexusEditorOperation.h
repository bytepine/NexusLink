// Copyright byteyang. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** 游戏线程上的长操作。操作与等待请求分开存活；终态不等于业务成功。 */
struct NEXUSLINK_API FNexusEditorOperation
{
	FString Id, Owner, Backend, State = TEXT("Running"), Message;
	TSharedPtr<FJsonObject> Result;
	TFunction<bool()> Stop;
	double StartedAt = 0;
	bool IsRunning() const { return State == TEXT("Running"); }
	TSharedPtr<FJsonObject> Snapshot() const;
};

/** 仅管理状态和有界等待，不负责调度编辑器工作。所有入口必须在游戏线程。 */
struct NEXUSLINK_API FNexusEditorOperations final
{
	static FString& RequestOwner();
	static TSharedPtr<FNexusEditorOperation> Create(const FString& Backend);
	static TSharedPtr<FNexusEditorOperation> Find(const FString& Id);
	static void Finish(const TSharedPtr<FNexusEditorOperation>& Operation, const FString& State,
		const TSharedPtr<FJsonObject>& Result, const FString& Message = FString());
	static bool Cancel(const FString& Id);
	static void Wait(const FString& Id, double Seconds, const FString& RequestKey,
		TFunction<void(TSharedPtr<FJsonObject>)> Complete,
		TFunction<void(const FString&)> Progress);
	static void CancelWait(const FString& RequestKey);
	static void ShutdownOwner(const FString& Owner);
	static void Reset();
};
