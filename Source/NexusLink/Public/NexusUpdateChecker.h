// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * NexusLink 插件版本检查器。
 * 从 GitHub Releases API 获取最新版本，与当前 VERSION 文件中的版本比较。
 * 异步执行，不阻塞主线程；失败时静默忽略（用户网络不通时无副作用）。
 */
struct NEXUSLINK_API FNexusUpdateChecker
{
	/** 读取当前插件版本（从 VERSION 文件）；若无法读取则返回 "unknown"。 */
	static FString GetCurrentVersion();

	/**
	 * 异步向 GitHub Releases API 请求最新版本，在 GameThread 调用 OnSuccess。
	 * 网络不通或解析失败时调用 OnError（若非空），并附带错误描述。
	 * @param OnSuccess  参数：bHasUpdate, LatestVersion, CurrentVersion
	 * @param OnError    可选；参数：ErrorDetail（人类可读的失败原因）
	 */
	static void CheckAsync(
		TFunction<void(bool bHasUpdate, FString LatestVersion, FString CurrentVersion)> OnSuccess,
		TFunction<void(FString ErrorDetail)> OnError = nullptr);

	/**
	 * 语义版本比较（格式 "X.Y.Z"）。
	 * 返回 true 表示 A 比 B 新（A > B）。
	 */
	static bool IsNewerVersion(const FString& A, const FString& B);
};
