// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class FJsonObject;

/**
 * 危险 Capability（tag=dangerous）确认门控。
 * Confirm 模式在 Execute 前弹出编辑器窗口，仅允许本次调用。
 */
struct NEXUSLINK_API FNexusDangerousCapGate
{
	FNexusDangerousCapGate() = delete;

	/** 注册表 HasTag(dangerous)。 */
	static bool IsDangerous(const FString& CapName);

	/** Confirm 模式且非会话强制启用。 */
	static bool NeedsConfirm(const FString& CapName);

	/** 从 arguments 抽出 command / code / scriptPath / mode，供弹窗与 reason 比对。 */
	static FString ExtractPayload(const TSharedPtr<FJsonObject>& Args);

	/**
	 * reason 去空白后 ≥24 字符，且不能只是 payload 复读。
	 * @return true 通过；失败时 OutError 为英文提示（给 AI）。
	 */
	static bool ValidateReason(const FString& Reason, const FString& Payload, FString& OutError);

	/**
	 * 不需要确认则返回空结果（FatalError 空）。
	 * 否则弹窗或在无 UI / 超时 / 批内已拒绝时返回 MakeUserDenied / MakeArgInvalidNoFeedback。
	 */
	static FCapabilityResult ConfirmOrDeny(const FString& CapName, const TSharedPtr<FJsonObject>& Args);

	/** call_capability 单条或批量入口：重置「批内已拒绝」标志。 */
	static void BeginBatch();
	static void EndBatch();

	struct FBatchScope
	{
		FBatchScope() { BeginBatch(); }
		~FBatchScope() { EndBatch(); }
	};
};
