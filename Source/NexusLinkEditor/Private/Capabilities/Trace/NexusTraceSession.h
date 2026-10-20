// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_TRACE_READ

class FJsonValue;

namespace TraceServices
{
class IAnalysisSession;
}

/** 进程内只保留一份 Trace 分析会话。打开、列文件、按 Store id 解析路径。 */
struct FNexusTraceSessionCache
{
	/** 打开已停录的 .utrace。另一份还在分析时不抢占，OutError 说明原因。 */
	static TSharedPtr<const TraceServices::IAnalysisSession> Open(const FString& TracePath, FString& OutError);

	/** TracePath 优先；否则用 TraceId 向本机 Trace Store 要文件路径。 */
	static bool ResolveTracePath(const TSharedPtr<FJsonObject>& Args, FString& OutPath, FString& OutError);

	/** Saved/Profiling 与本机 Store，按修改时间倒序，最多 Limit 条。 */
	static void CollectRecentTraces(int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutTraces);
};

#endif
