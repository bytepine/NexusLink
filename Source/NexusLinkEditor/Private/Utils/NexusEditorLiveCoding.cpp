// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusVersionCompat.h"
#include "Modules/ModuleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "Containers/Ticker.h"
#if PLATFORM_WINDOWS && WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

struct FNexusCompileLog final : FOutputDevice
{
	FCriticalSection Mutex;
	FString Text;
	bool bSuccess = false, bFailure = false, bNoChanges = false, bPatch = false;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual void Serialize(const TCHAR* Value, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category != FName(TEXT("LogLiveCoding"))) return;
		FScopeLock Lock(&Mutex);
		const FString Line(Value);
		if (Text.Len() < 65536) Text += Line + TEXT("\n");
		bSuccess |= Line.Contains(TEXT("Live coding succeeded"));
		bFailure |= Line.Contains(TEXT("Live coding failed")) || Line.Contains(TEXT("Unable to start live coding"));
		bNoChanges |= Line.Contains(TEXT("no code changes detected"));
	}
};

static TSharedPtr<FNexusCompileLog> GNexusCompileCapture;
static FDelegateHandle GNexusCompilePatchHandle;
void FNexusEditorToolsets::ShutdownLiveCoding()
{
#if PLATFORM_WINDOWS && WITH_LIVE_CODING
 if (GNexusCompileCapture.IsValid()) GLog->RemoveOutputDevice(GNexusCompileCapture.Get());
 auto* Module = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
 if (Module) Module->GetOnPatchCompleteDelegate().Remove(GNexusCompilePatchHandle);
#endif
 GNexusCompileCapture.Reset();
}

TSharedPtr<FNexusEditorOperation> FNexusEditorToolsets::Compile()
{
	auto Operation = FNexusEditorOperations::Create(TEXT("EngineLiveCoding"));
	if (!Operation.IsValid()) return nullptr;
#if PLATFORM_WINDOWS && WITH_LIVE_CODING
	auto* Module = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!Module || !Module->IsEnabledForSession() || Module->IsCompiling())
	{
		FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Live Coding must be enabled and idle before compiling"));
		return Operation;
	}
	auto Log = MakeShared<FNexusCompileLog>(); GNexusCompileCapture = Log; GLog->AddOutputDevice(&Log.Get());
	const auto PatchHandle = Module->GetOnPatchCompleteDelegate().AddLambda([Log]() { Log->bPatch = true; });
	GNexusCompilePatchHandle = PatchHandle;
#if NX_UE_HAS_LIVE_CODING_RESULT
	ELiveCodingCompileResult StartResult = ELiveCodingCompileResult::NotStarted;
	const bool bStarted = Module->Compile(ELiveCodingCompileFlags::None, &StartResult);
#else
	Module->Compile();
	const bool bStarted = Module->IsCompiling();
#endif
	if (!bStarted)
	{
		GLog->RemoveOutputDevice(&Log.Get()); GNexusCompileCapture.Reset(); Module->GetOnPatchCompleteDelegate().Remove(PatchHandle);
		FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Compile did not start")); return Operation;
	}
	Operation->Message = TEXT("Compiling; cancellation is unavailable. Asset save protections remain active.");
	auto Tick = FTickerDelegate::CreateLambda([Operation, Log, PatchHandle](float)
	{
		auto* Current = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
		if (Current && Current->IsCompiling() && Operation->IsRunning()) return true;
		GLog->RemoveOutputDevice(&Log.Get()); GNexusCompileCapture.Reset();
		if (Current) Current->GetOnPatchCompleteDelegate().Remove(PatchHandle);
		FScopeLock Lock(&Log->Mutex);
		auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("Diagnostics"), Log->Text);
		Result->SetBoolField(TEXT("bPatchApplied"), Log->bPatch);
		Result->SetStringField(TEXT("Outcome"), Log->bFailure ? TEXT("Failure") : Log->bNoChanges ? TEXT("NoChanges") : (Log->bSuccess || Log->bPatch) ? TEXT("Success") : TEXT("Unknown"));
		FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
		return false;
	});
FNexusEditorToolsets::AddTicker(Tick, 0.2f);
#else
	FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Live Coding is unavailable on this platform/build"));
#endif
	return Operation;
}
