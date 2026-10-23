// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_TRACE_READ

#include "Utils/NexusEditorTraceUtils.h"

/** 进程内只保留一份 Trace 分析会话。打开文件或 Store 流，列出最近记录。 */
struct NEXUSLINKEDITOR_API FNexusTraceSessionCache
{
	/** TracePath 优先，否则打开本机 Store id。无真实路径时 OutPath 留空。 */
	static TSharedPtr<const FNexusTraceAnalysisSession> Open(const TSharedPtr<FJsonObject>& Args,
		FString& OutPath, FString& OutError, uint32 StorePort = 0);

	/** Saved/Profiling 与本机 Store，按修改时间倒序，最多 Limit 条。 */
	static void CollectRecentTraces(int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutTraces, uint32 StorePort = 0);
};

#endif
