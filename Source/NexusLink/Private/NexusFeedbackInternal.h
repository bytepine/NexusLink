// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDateTime;

/**
 * FNexusFeedback 记录路径（NexusFeedback.cpp）与报告路径（NexusFeedbackReport.cpp）
 * 共用的内部助手；定义留在 NexusFeedback.cpp。禁止 namespace（CapabilitySpec §8.3），
 * 用 struct final + 全 static 代替。
 */
struct FNexusFeedbackInternal final
{
	FNexusFeedbackInternal() = delete;

	/**
	 * 规则化字符串：将数字序列、引号内字符串、UE 包路径（/Game/... /Engine/...）替换为 *，
	 * 使同类但参数不同的错误命中同一节流 key / 指纹。
	 */
	static FString NormalizeForThrottle(const FString& Input);

	/** 将 FDateTime 序列化为 ISO 8601 UTC 字符串。 */
	static FString DateTimeToIso8601(const FDateTime& Dt);

	/** 当前进程 NexusLink 插件 VersionName（失败时 "unknown"）。 */
	static FString GetLivePluginVersion();

	/** 当前引擎版本字符串，如 "4.26.2"。 */
	static FString GetLiveUeVersion();

	/** 当前 ToolsListMode 字符串。 */
	static FString GetLiveToolsListMode();
};
