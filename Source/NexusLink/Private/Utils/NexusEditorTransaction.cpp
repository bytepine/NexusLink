// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorTransaction.h"
#include "NexusMcpTool.h"

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
	return FNexusEditorServices::IsTransactionActive();
}

int32 FNexusEditorTransaction::GetActiveRecordCount()
{
	return FNexusEditorServices::GetActiveTransactionRecordCount();
}

TUniquePtr<INexusTransactionHandle> FNexusEditorTransaction::Begin(const FString& Title)
{
	return FNexusEditorServices::BeginTransaction(Title);
}

void FNexusEditorTransaction::CancelAndRevert(TUniquePtr<INexusTransactionHandle>& Tx)
{
	if (!Tx.IsValid())
	{
		return;
	}
	Tx->CancelAndRevert();
	Tx.Reset();
}
