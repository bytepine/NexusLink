// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusDangerousCapGate.h"
#include "NexusCapabilityRegistry.h"
#include "NexusEditorServices.h"
#include "NexusLinkSettings.h"
#include "NexusMcpTool.h"
#include "Log/NexusLogCapture.h"
#include "Utils/NexusVersionCompat.h"
#include "Dom/JsonObject.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNexusDangerousCap, Log, All);

static bool GDangerousCapBatchActive = false;
static bool GDangerousCapBatchDenied = false;

static const TCHAR* GAuditCategory = TEXT("LogNexusDangerousCap");
static const int32 GReasonMinLen = 24;

static FString NormalizeWs(FString S)
{
	S.TrimStartAndEndInline();
	S.ReplaceInline(TEXT("\r\n"), TEXT(" "));
	S.ReplaceInline(TEXT("\n"), TEXT(" "));
	S.ReplaceInline(TEXT("\t"), TEXT(" "));
	while (S.ReplaceInline(TEXT("  "), TEXT(" "))) {}
	return S;
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

	// 实际 Slate 模态框由 NexusLinkEditor 通过 FNexusEditorServices::PromptDangerousCapConfirm 钩子安装；
	// 未安装钩子（如纯 Runtime 模块加载、钩子尚未装好）时该 TFunction 默认实现直接回 user_denied。
	FCapabilityResult Result = FNexusEditorServices::PromptDangerousCapConfirm(CapName, NormalizeWs(Reason), Payload);
	Audit(CapName, Result.FatalError.IsEmpty() ? TEXT("allowed") : TEXT("denied"),
		Result.FatalError.IsEmpty() ? TEXT("this request") : Result.FatalError);
	if (!Result.FatalError.IsEmpty() && GDangerousCapBatchActive)
	{
		GDangerousCapBatchDenied = true;
	}
	return Result;
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
