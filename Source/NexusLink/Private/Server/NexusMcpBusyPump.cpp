// Copyright byteyang. All Rights Reserved.
#include "Server/NexusMcpBusyPump.h"
#include "Utils/NexusVersionCompat.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Misc/SlowTask.h"
#if WITH_EDITOR && PLATFORM_WINDOWS && NX_UE_HAS_HTTP_LISTENER_TICK
#include "Windows/WindowsHWrapper.h"
static TMap<UINT_PTR, TFunction<void()>> GNexusModalTimers;
static void CALLBACK NexusModalTimerProcedure(HWND, UINT, UINT_PTR TimerId, DWORD)
{
	if (const TFunction<void()>* Callback = GNexusModalTimers.Find(TimerId)) (*Callback)();
}
#endif

int32 FNexusMcpBusyPump::NativeModalDepth = 0;
bool FNexusMcpBusyPump::bInsideBlockedPump = false;
bool FNexusMcpBusyPump::bInsideSlowTaskProgress = false;

FString FNexusMcpBusyPump::GetReason()
{
	if (!GIsEditor) return FString();
	if (GIsSlowTask || bInsideSlowTaskProgress) return TEXT("SlowTaskInProgress");
	if (NativeModalDepth > 0 || bInsideBlockedPump ||
		(FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid())) return TEXT("ModalDialogOpen");
	return FString();
}

FNexusMcpBusyPump::FNexusMcpBusyPump(TFunction<void(float)> InService)
	: ServiceCallback(MoveTemp(InService))
{
#if WITH_EDITOR
	if (!GIsEditor) return;
	PreModalHandle = FCoreDelegates::PreModal.AddLambda([]() { if (IsInGameThread()) ++NativeModalDepth; });
	PostModalHandle = FCoreDelegates::PostModal.AddLambda([]() { if (IsInGameThread()) NativeModalDepth = FMath::Max(0, NativeModalDepth - 1); });
#if NX_UE_HAS_HTTP_LISTENER_TICK
	if (FSlateApplication::IsInitialized())
		ModalTickHandle = FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FNexusMcpBusyPump::Service);
	ProgressHandle = FSlowTask::OnProgress().AddLambda([this]()
	{
		if (!IsInGameThread()) return;
		// Unattended 等模式不会置 GIsSlowTask；进度回调本身仍代表不能重入业务的范围。
		TGuardValue<bool> ProgressGuard(bInsideSlowTaskProgress, true);
		Service(0.1f);
	});
#if PLATFORM_WINDOWS
	NativeTimer = ::SetTimer(nullptr, 0, 100, &NexusModalTimerProcedure);
	if (NativeTimer) GNexusModalTimers.Add(static_cast<UINT_PTR>(NativeTimer), [this]() { Service(0.1f); });
#endif
#endif
#endif
}

FNexusMcpBusyPump::~FNexusMcpBusyPump()
{
#if WITH_EDITOR
	FCoreDelegates::PreModal.Remove(PreModalHandle);
	FCoreDelegates::PostModal.Remove(PostModalHandle);
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().GetOnModalLoopTickEvent().Remove(ModalTickHandle);
#if NX_UE_HAS_HTTP_LISTENER_TICK
	FSlowTask::OnProgress().Remove(ProgressHandle);
#if PLATFORM_WINDOWS
	if (NativeTimer) { ::KillTimer(nullptr, static_cast<UINT_PTR>(NativeTimer)); GNexusModalTimers.Remove(static_cast<UINT_PTR>(NativeTimer)); }
#endif
#endif
#endif
}

void FNexusMcpBusyPump::Service(float DeltaTime)
{
	if (bServicing || !IsInGameThread() || GetReason().IsEmpty() || FPlatformTime::Seconds() - LastServiceAt < 0.05) return;
	const double Now = FPlatformTime::Seconds();
	DeltaTime = LastServiceAt > 0 ? static_cast<float>(Now - LastServiceAt) : DeltaTime;
	LastServiceAt = Now;
	TGuardValue<bool> ServiceGuard(bServicing, true);
	TGuardValue<bool> BlockedGuard(bInsideBlockedPump, true);
	ServiceCallback(FMath::Clamp(DeltaTime, 0.0f, 0.125f));
}
