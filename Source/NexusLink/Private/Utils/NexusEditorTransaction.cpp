// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorTransaction.h"
#include "NexusMcpTool.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Misc/ITransaction.h"
#endif

bool FNexusEditorTransaction::ShouldTransact(const FString& CapName, const TArray<FString>& Tags)
{
	if (!Tags.Contains(FNexusMcpTags::Write))
	{
		return false;
	}
	if (Tags.Contains(FNexusMcpTags::Readonly) || Tags.Contains(FNexusMcpTags::Runtime))
	{
		return false;
	}
	if (CapName == TEXT("save_asset") || CapName == TEXT("compile_blueprint")
		|| CapName == TEXT("unload_asset") || CapName == TEXT("control_pie")
		|| CapName == TEXT("exec_command") || CapName == TEXT("capture_viewport")
		|| CapName == TEXT("control_movie_pipeline"))
	{
		return false;
	}
	if (CapName.Contains(TEXT("_runtime_")) || CapName.Contains(TEXT("_lua")))
	{
		return false;
	}
	return true;
}

bool FNexusEditorTransaction::IsTransactionActive()
{
#if WITH_EDITOR
	return GEditor && GEditor->IsTransactionActive();
#else
	return false;
#endif
}

#if WITH_EDITOR
TUniquePtr<FScopedTransaction> FNexusEditorTransaction::Begin(const FString& Title)
{
	if (!GEditor || IsTransactionActive())
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
	return Tx;
}

void FNexusEditorTransaction::CancelAndRevert(TUniquePtr<FScopedTransaction>& Tx)
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
#endif
