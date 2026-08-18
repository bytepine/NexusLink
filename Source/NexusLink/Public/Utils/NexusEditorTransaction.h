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

#if WITH_EDITOR
	/** 开启命名事务；GEditor 不可用或已在事务中返回 nullptr。 */
	static TUniquePtr<FScopedTransaction> Begin(const FString& Title);

	/** 先 Apply 还原内存再 Cancel 丢弃（引擎 Cancel 本身不还原对象）。 */
	static void CancelAndRevert(TUniquePtr<FScopedTransaction>& Tx);
#endif
};
