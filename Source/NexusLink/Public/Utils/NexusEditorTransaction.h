// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusEditorServices.h"

/**
 * 编辑器 Undo 事务包装。仅包内存 Execute，不包 save/compile。
 * 已在事务中则 Begin 返回空（供 calls[] 外层独占一笔 Ctrl+Z）。
 *
 * 本类驻留 NexusLink（Runtime）模块，供 FNexusCapability::Run / NexusMcpToolCallCapability
 * 共用；真正触碰 GEditor / FScopedTransaction 的实现经 FNexusEditorServices 钩子转发到
 * NexusLinkEditor，避免 Runtime 模块链接 UnrealEd。未安装钩子（独立 Game/DS 包）时
 * Begin 返回空句柄、IsTransactionActive/GetActiveRecordCount 返回 false/0，行为等价于「无事务」。
 */
struct NEXUSLINK_API FNexusEditorTransaction final
{
	FNexusEditorTransaction() = delete;

	/** Write 且非 Readonly/Runtime，且不在磁盘/PIE/Lua/命令黑名单。纯标签逻辑，不经钩子。 */
	static bool ShouldTransact(const FString& CapName, const TArray<FString>& Tags);

	/** 当前是否已有未结束的编辑器事务。 */
	static bool IsTransactionActive();

	/**
	 * 当前事务已记录的对象数；无事务或未安装钩子返回 0。
	 * 供脚本类 cap 前后取差，判断本次执行是否真产生了可回滚记录。
	 */
	static int32 GetActiveRecordCount();

	/** 开启命名事务；未安装钩子、GEditor 不可用或已在事务中返回 nullptr。 */
	static TUniquePtr<INexusTransactionHandle> Begin(const FString& Title);

	/** 先还原内存快照再丢弃这笔 undo 记录。 */
	static void CancelAndRevert(TUniquePtr<INexusTransactionHandle>& Tx);
};
