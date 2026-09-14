// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorTransactionImpl.h"
#include "NexusEditorServices.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Editor/Transactor.h"
#include "Misc/ITransaction.h"

namespace
{
	/** FScopedTransaction 的不透明句柄包装：Runtime 侧只认 INexusTransactionHandle。 */
	class FNexusScopedTransactionHandle final : public INexusTransactionHandle
	{
	public:
		explicit FNexusScopedTransactionHandle(TUniquePtr<FScopedTransaction> InTx)
			: Tx(MoveTemp(InTx))
		{}

		virtual void CancelAndRevert() override
		{
			if (!Tx.IsValid() || !Tx->IsOutstanding())
			{
				return;
			}
			// 视口 AbortTracking 同款：Apply 把对象拉回 Modify 快照，Cancel 只从 undo 栈丢掉该笔
			if (GUndo)
			{
				GUndo->Apply();
			}
			Tx->Cancel();
			Tx.Reset();
		}

		virtual bool IsOutstanding() const override
		{
			return Tx.IsValid() && Tx->IsOutstanding();
		}

	private:
		TUniquePtr<FScopedTransaction> Tx;
	};

	bool ImplIsTransactionActive()
	{
		return GEditor && GEditor->IsTransactionActive();
	}

	int32 ImplGetActiveRecordCount()
	{
		// 编辑器里 GUndo 恒为 UTransBuffer 用 FTransaction 建的，引擎自身（UTransBuffer::End）
		// 也做同样的向下转型；对象在 Modify() 时即入 Records，无需等 Finalize
		if (const FTransaction* Tx = static_cast<const FTransaction*>(GUndo))
		{
			return Tx->GetRecordCount();
		}
		return 0;
	}

	TUniquePtr<INexusTransactionHandle> ImplBeginTransaction(const FString& Title)
	{
		if (!GEditor || ImplIsTransactionActive())
		{
			return nullptr;
		}
		const FString Session = FString::Printf(TEXT("MCP: %s"), Title.IsEmpty() ? TEXT("capability") : *Title);
		TUniquePtr<FScopedTransaction> Tx = MakeUnique<FScopedTransaction>(FText::FromString(Session));
		// Construct 在 GEditor->Trans 为空或 GIsTransacting 时 Index=-1，对象仍在但无法回滚
		if (!Tx.IsValid() || !Tx->IsOutstanding())
		{
			return nullptr;
		}
		return MakeUnique<FNexusScopedTransactionHandle>(MoveTemp(Tx));
	}
}

void NexusInstallEditorTransactionHooks()
{
	FNexusEditorServices::IsTransactionActive = &ImplIsTransactionActive;
	FNexusEditorServices::GetActiveTransactionRecordCount = &ImplGetActiveRecordCount;
	FNexusEditorServices::BeginTransaction = &ImplBeginTransaction;
}

#else // !WITH_EDITOR

void NexusInstallEditorTransactionHooks() {}

#endif // WITH_EDITOR
