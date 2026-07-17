// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * NexusLink 插件版本检查器。
 * 从 GitHub releases/latest 重定向获取最新 Release tag，与 .uplugin VersionName 比较。
 * 异步执行，不阻塞主线程；失败时静默忽略（用户网络不通时无副作用）。
 */
struct NEXUSLINK_API FNexusUpdateChecker
{
	/** 读取当前插件版本（NexusLink.uplugin 的 VersionName）；若无法读取则返回 "unknown"。 */
	static FString GetCurrentVersion();

	/**
	 * 从 Release 落地页 HTML 解析 GitHub 标记的「Latest」版本 tag。
	 * 优先 og:url / canonical，兜底取全文首个 /releases/tag/；去除前导 "v"。
	 * 未找到时返回空字符串。
	 */
	static FString ParseLatestTagFromReleasePage(const FString& Html);

	/**
	 * 异步请求 GitHub releases/latest，在 GameThread 调用 OnSuccess。
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
