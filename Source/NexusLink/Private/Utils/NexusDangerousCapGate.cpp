// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusDangerousCapGate.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusMcpTool.h"
#include "Editor/NexusLogCapture.h"
#include "Utils/NexusVersionCompat.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#if NX_UE_HAS_APP_STYLE
#include "Styling/AppStyle.h"
#else
#include "EditorStyleSet.h"
#endif
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNexusDangerousCap, Log, All);

#if NX_UE_HAS_FTSTICKER
using FNexusTicker = FTSTicker;
#else
using FNexusTicker = FTicker;
#endif

static bool GDangerousCapBatchActive = false;
static bool GDangerousCapBatchDenied = false;
static bool GDangerousCapConfirmInProgress = false;

static const TCHAR* GAuditCategory = TEXT("LogNexusDangerousCap");
static const int32 GReasonMinLen = 24;
static const int32 GPayloadDialogMax = 1500;
static const int32 GReasonDialogMax = 800;

static FString NormalizeWs(FString S)
{
	S.TrimStartAndEndInline();
	S.ReplaceInline(TEXT("\r\n"), TEXT(" "));
	S.ReplaceInline(TEXT("\n"), TEXT(" "));
	S.ReplaceInline(TEXT("\t"), TEXT(" "));
	while (S.ReplaceInline(TEXT("  "), TEXT(" "))) {}
	return S;
}

static FString TruncateForUi(const FString& S, int32 MaxChars)
{
	if (S.Len() <= MaxChars)
	{
		return S;
	}
	return S.Left(MaxChars) + FString::Printf(TEXT("… (%d chars truncated)"), S.Len() - MaxChars);
}

static void Audit(const FString& CapName, const TCHAR* Outcome, const FString& Detail)
{
	const FString Msg = FString::Printf(TEXT("[%s] %s %s"), *CapName, Outcome, *Detail);
	UE_LOG(LogNexusDangerousCap, Log, TEXT("%s"), *Msg);
	FNexusLogCapture::Get().AppendEntry(GAuditCategory, ELogVerbosity::Log, Msg);
}

static bool CanShowConfirmUi()
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return false;
	}
#if WITH_EDITOR
	return FSlateApplication::IsInitialized();
#else
	return false;
#endif
}

bool FNexusDangerousCapGate::IsDangerous(const FString& CapName)
{
	return UNexusLinkSettings::IsDangerousCapability(CapName);
}

bool FNexusDangerousCapGate::NeedsConfirm(const FString& CapName)
{
	if (!IsDangerous(CapName))
	{
		return false;
	}
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!Settings)
	{
		return false;
	}
	if (Settings->SessionEnabledCapabilities.Contains(CapName))
	{
		return false;
	}
	return Settings->DangerousCapAccess == ENexusDangerousCapAccess::Confirm
		&& !Settings->DisabledCapabilities.Contains(CapName);
}

FString FNexusDangerousCapGate::ExtractPayload(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FString();
	}
	FString Out;
	auto AppendField = [&](const TCHAR* Key)
	{
		FString V;
		if (Args->TryGetStringField(Key, V) && !V.IsEmpty())
		{
			if (!Out.IsEmpty())
			{
				Out += TEXT("\n");
			}
			Out += FString::Printf(TEXT("%s: %s"), Key, *V);
		}
	};
	AppendField(TEXT("mode"));
	AppendField(TEXT("command"));
	AppendField(TEXT("code"));
	AppendField(TEXT("scriptPath"));
	return Out;
}

bool FNexusDangerousCapGate::ValidateReason(const FString& Reason, const FString& Payload, FString& OutError)
{
	const FString ReasonN = NormalizeWs(Reason);
	if (ReasonN.Len() < GReasonMinLen)
	{
		OutError = FString::Printf(
			TEXT("Missing or too short 'reason' (need ≥%d chars after trim). Describe the purpose, expected effect, and why no safer dedicated capability exists."),
			GReasonMinLen);
		return false;
	}
	const FString PayloadN = NormalizeWs(Payload);
	if (!PayloadN.IsEmpty())
	{
		FString StrippedPayload = PayloadN;
		StrippedPayload.ReplaceInline(TEXT("command: "), TEXT(""));
		StrippedPayload.ReplaceInline(TEXT("code: "), TEXT(""));
		StrippedPayload.ReplaceInline(TEXT("scriptPath: "), TEXT(""));
		StrippedPayload.ReplaceInline(TEXT("mode: "), TEXT(""));
		if (ReasonN.Equals(PayloadN, ESearchCase::IgnoreCase)
			|| ReasonN.Equals(StrippedPayload, ESearchCase::IgnoreCase)
			|| (PayloadN.Contains(ReasonN) && ReasonN.Len() + 8 <= PayloadN.Len()))
		{
			OutError = TEXT("'reason' must not simply repeat the payload. Explain purpose, expected effect, and why no safer dedicated capability exists.");
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
static FCapabilityResult PromptUser(const FString& CapName, const FString& Reason, const FString& Payload)
{
	if (GDangerousCapConfirmInProgress)
	{
		Audit(CapName, TEXT("denied"), TEXT("nested confirm"));
		return FCapabilityResult::MakeUserDenied(
			TEXT("Another dangerous-capability confirmation is already open. Do not retry until the user finishes that prompt."));
	}

	struct FConfirmState
	{
		bool bResponded = false;
		bool bAllowed = false;
	};
	TSharedRef<FConfirmState> State = MakeShared<FConfirmState>();

	const FString Body = FString::Printf(
		TEXT("Capability：%s\n风险：脚本逃生舱，等价于进程内任意代码/控制台执行；事务可能半回滚，也可能导致编辑器崩溃。\n\nAI 声称的理由（不可验证）：\n%s\n\n将执行：\n%s\n\n全屏 PIE 下若看不到本窗口，请先退出沉浸模式。\n允许 = 仅本次；拒绝 = 不执行。超时将自动拒绝。"),
		*CapName,
		*TruncateForUi(Reason, GReasonDialogMax),
		*TruncateForUi(Payload.IsEmpty() ? TEXT("（无 command/code/scriptPath）") : Payload, GPayloadDialogMax));

	TSharedRef<SWindow> Win = SNew(SWindow)
		.Title(NSLOCTEXT("NexusLink", "DangerCapTitle", "NexusLink — 允许执行危险 Capability？"))
		.ClientSize(FVector2D(620.0f, 480.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::FixedSize);

	Win->SetContent(
		SNew(SBorder)
		.Padding(12.0f)
#if NX_UE_HAS_APP_STYLE
		// 走 Get().GetBrush：静态 FAppStyle::GetBrush 直到 5.1 才有，5.0 只有 Get()
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
#else
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
#endif
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(Body))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(NSLOCTEXT("NexusLink", "DangerCapAllow", "允许本次"))
					.OnClicked_Lambda([State, Win]()
					{
						State->bAllowed = true;
						State->bResponded = true;
						Win->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("NexusLink", "DangerCapDeny", "拒绝"))
					.OnClicked_Lambda([State, Win]()
					{
						State->bAllowed = false;
						State->bResponded = true;
						Win->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]);

	GDangerousCapConfirmInProgress = true;
	FSlateApplication::Get().AddWindow(Win, true);
	Win->BringToFront(true);
	Win->HACK_ForceToFront();
	Win->FlashWindow();
	Win->SetOnWindowClosed(FOnWindowClosed::CreateLambda([State](const TSharedRef<SWindow>&)
	{
		if (!State->bResponded)
		{
			State->bAllowed = false;
			State->bResponded = true;
		}
	}));

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	const int32 TimeoutSec = Settings ? Settings->DangerousCapConfirmTimeoutSec : 90;
	const double Start = FPlatformTime::Seconds();
	bool bTimedOut = false;

	while (!State->bResponded)
	{
		const float Delta = 0.016f;
		FNexusTicker::GetCoreTicker().Tick(Delta);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().PumpMessages();
			FSlateApplication::Get().Tick();
		}
		FPlatformProcess::Sleep(0.01f);

		if (TimeoutSec > 0 && (FPlatformTime::Seconds() - Start) >= static_cast<double>(TimeoutSec))
		{
			bTimedOut = true;
			break;
		}
	}

	if (Win->IsVisible())
	{
		Win->RequestDestroyWindow();
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
	}
	GDangerousCapConfirmInProgress = false;

	if (bTimedOut && !State->bResponded)
	{
		Audit(CapName, TEXT("timeout"), FString::Printf(TEXT("%ds"), TimeoutSec));
		if (GDangerousCapBatchActive)
		{
			GDangerousCapBatchDenied = true;
		}
		return FCapabilityResult::MakeUserDenied(
			TEXT("User confirmation timed out; this dangerous capability was not executed. Do not retry unless the user asks."));
	}
	if (State->bAllowed)
	{
		Audit(CapName, TEXT("allowed"), TEXT("this request"));
		return FCapabilityResult();
	}
	Audit(CapName, TEXT("denied"), TEXT("user"));
	if (GDangerousCapBatchActive)
	{
		GDangerousCapBatchDenied = true;
	}
	return FCapabilityResult::MakeUserDenied(
		TEXT("User denied this dangerous capability request. Do not retry; user denied this request."));
}
#endif

FCapabilityResult FNexusDangerousCapGate::ConfirmOrDeny(const FString& CapName, const TSharedPtr<FJsonObject>& Args)
{
	if (!NeedsConfirm(CapName))
	{
		return FCapabilityResult();
	}

	if (GDangerousCapBatchActive && GDangerousCapBatchDenied)
	{
		Audit(CapName, TEXT("denied"), TEXT("batch skip"));
		return FCapabilityResult::MakeUserDenied(
			TEXT("A previous dangerous capability in this batch was denied; remaining dangerous calls were skipped. Do not retry."));
	}

	const FString Payload = ExtractPayload(Args);
	FString Reason;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("reason"), Reason);
	}
	FString ReasonErr;
	if (!ValidateReason(Reason, Payload, ReasonErr))
	{
		return FCapabilityResult::MakeArgInvalidNoFeedback(ReasonErr);
	}

	if (!CanShowConfirmUi())
	{
		Audit(CapName, TEXT("denied"), TEXT("no UI"));
		if (GDangerousCapBatchActive)
		{
			GDangerousCapBatchDenied = true;
		}
		return FCapabilityResult::MakeUserDenied(
			TEXT("No editor UI to confirm this dangerous capability (unattended / commandlet / headless). Do not retry."));
	}

#if WITH_EDITOR
	return PromptUser(CapName, NormalizeWs(Reason), Payload);
#else
	Audit(CapName, TEXT("denied"), TEXT("no editor"));
	return FCapabilityResult::MakeUserDenied(
		TEXT("No editor UI to confirm this dangerous capability. Do not retry."));
#endif
}

void FNexusDangerousCapGate::BeginBatch()
{
	GDangerousCapBatchActive = true;
	GDangerousCapBatchDenied = false;
}

void FNexusDangerousCapGate::EndBatch()
{
	GDangerousCapBatchActive = false;
	GDangerousCapBatchDenied = false;
}
