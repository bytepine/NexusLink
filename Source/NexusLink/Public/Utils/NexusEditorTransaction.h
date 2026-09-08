// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
class FScopedTransaction;
#endif

/**
 * 编辑器 Undo 事务包装。仅包内存 Execute，不包 save/compile。
 * 已在事务中则 Begin 返回空（供 calls[] 外层独占一笔 Ctrl+Z）。
 */
struct NEXUSLINK_API FNexusEditorTransaction final
{
	FNexusEditorTransaction() = delete;

	/** Write 且非 Readonly/Runtime，且不在磁盘/PIE/Lua/命令黑名单。 */
	static bool ShouldTransact(const FString& CapName, const TArray<FString>& Tags);

	/** 当前是否已有未结束的编辑器事务。 */
	static bool IsTransactionActive();

	/**
	 * 当前事务已记录的对象数；无事务或非编辑器返回 0。
	 * 供脚本类 cap 前后取差，判断本次执行是否真产生了可回滚记录
	 * （`ITransaction::IsTransient()` 依赖 Finalize 后才算出的 DeltaChange，事务未结束时用不了）。
	 */
	static int32 GetActiveRecordCount();

#if WITH_EDITOR
	/** 开启命名事务；GEditor 不可用或已在事务中返回 nullptr。 */
	static TUniquePtr<FScopedTransaction> Begin(const FString& Title);

	/** 先 Apply 还原内存再 Cancel 丢弃（引擎 Cancel 本身不还原对象）。 */
	static void CancelAndRevert(TUniquePtr<FScopedTransaction>& Tx);
#endif
};
