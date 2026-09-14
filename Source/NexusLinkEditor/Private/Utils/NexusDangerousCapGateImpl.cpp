// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusDangerousCapGateImpl.h"
#include "NexusEditorServices.h"
#include "NexusCapability.h"
#include "NexusLinkSettings.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
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

#define LOCTEXT_NAMESPACE "FNexusDangerousCapGateImpl"

#if NX_UE_HAS_FTSTICKER
using FNexusTicker = FTSTicker;
#else
using FNexusTicker = FTicker;
#endif

namespace
{
	bool GDangerousCapConfirmInProgress = false;
	constexpr int32 GPayloadDialogMax = 1500;
	constexpr int32 GReasonDialogMax = 800;

	FString TruncateForUi(const FString& S, int32 MaxChars)
	{
		if (S.Len() <= MaxChars)
		{
			return S;
		}
		return S.Left(MaxChars) + FString::Printf(TEXT("… (%d chars truncated)"), S.Len() - MaxChars);
	}

	/** Slate 模态确认框：允许本次 / 拒绝 / 超时自动拒绝。安装到 FNexusEditorServices::PromptDangerousCapConfirm。 */
	FCapabilityResult ImplPromptDangerousCapConfirm(const FString& CapName, const FString& Reason, const FString& Payload)
	{
		if (GDangerousCapConfirmInProgress)
		{
			return FCapabilityResult::MakeUserDenied(
				TEXT("Another dangerous-capability confirmation is already open (nested confirm). Do not retry until the user finishes that prompt."));
		}
		if (!FSlateApplication::IsInitialized())
		{
			return FCapabilityResult::MakeUserDenied(
				TEXT("No editor UI to confirm this dangerous capability. Do not retry."));
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
			return FCapabilityResult::MakeUserDenied(
				FString::Printf(TEXT("User confirmation timed out (%ds); this dangerous capability was not executed. Do not retry unless the user asks."), TimeoutSec));
		}
		if (State->bAllowed)
		{
			return FCapabilityResult();
		}
		return FCapabilityResult::MakeUserDenied(
			TEXT("User denied this dangerous capability request. Do not retry; user denied this request."));
	}
}

void NexusInstallDangerousCapGateHooks()
{
	FNexusEditorServices::PromptDangerousCapConfirm = &ImplPromptDangerousCapConfirm;
}

#undef LOCTEXT_NAMESPACE

#else // !WITH_EDITOR

void NexusInstallDangerousCapGateHooks() {}

#endif // WITH_EDITOR
