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
	if (CapName == TEXT("SaveAsset") || CapName == TEXT("CompileBlueprint")
		|| CapName == TEXT("UnloadAsset") || CapName == TEXT("ControlPie")
		|| CapName == TEXT("ExecCommand") || CapName == TEXT("CaptureViewport")
		|| CapName == TEXT("ControlMoviePipeline"))
	{
		return false;
	}
	if (CapName.Contains(TEXT("_runtime_")))
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
