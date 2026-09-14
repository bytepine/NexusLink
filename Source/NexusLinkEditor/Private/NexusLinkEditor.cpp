// Copyright byteyang. All Rights Reserved.

#include "NexusLinkEditor.h"
#include "NexusEditorServices.h"
#include "NexusLinkSettings.h"
#include "NexusUpdateChecker.h"
#include "Editor/NexusEditorStatusBar.h"
#include "Editor/NexusLinkSettingsCustomization.h"
#include "Utils/NexusEditorTransactionImpl.h"
#include "Utils/NexusDangerousCapGateImpl.h"
#include "Utils/NexusPackageLedgerImpl.h"
#include "Utils/NexusPortUtilsImpl.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Utils/NexusVersionCompat.h"

#define LOCTEXT_NAMESPACE "FNexusLinkEditorModule"

#if NX_UE_HAS_FTSTICKER
using FNexusEditorTicker = FTSTicker;
#else
using FNexusEditorTicker = FTicker;
#endif

namespace
{
	/** 延迟一帧执行回调，避免在引擎初始化回调中直接打开模态对话框/通知。 */
	void CallNextTick(TFunction<void()> Callback)
	{
		FNexusEditorTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Callback = MoveTemp(Callback)](float) -> bool
			{
				Callback();
				return false;
			}),
			0.0f
		);
	}
}

void FNexusLinkEditorModule::StartupModule()
{
	// FNexusEditorServices 钩子表：装真实编辑器实现（事务 / 危险 Capability 确认弹窗 /
	// 资产 finalize / 包卸载 / 打开设置面板），Runtime 模块通过钩子反向调用
	NexusInstallEditorTransactionHooks();
	NexusInstallDangerousCapGateHooks();
	NexusInstallPackageLedgerHooks();
	NexusInstallPortUtilsHooks();
	NexusInstallAssetEditorUtilsHooks();

	// 服务器启停通知钩子：状态栏是本模块专属 UI
	FNexusEditorServices::NotifyServerStarted = [](int32 McpPort, int32 WsPort)
	{
		if (GIsEditor)
		{
			FNexusEditorStatusBar::Register(McpPort, WsPort);
		}
	};
	FNexusEditorServices::NotifyServerStopped = []()
	{
		if (GIsEditor)
		{
			FNexusEditorStatusBar::Unregister();
		}
	};

	// 设置面板仅完整 Editor UI；Editor.exe -server/-game 时 GIsEditor=false
	if (GIsEditor)
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(
			UNexusLinkSettings::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FNexusLinkSettingsCustomization::MakeInstance));
	}

#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FNexusLinkEditorModule::OnPostEngineInit);
#else
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNexusLinkEditorModule::OnPostEngineInit);
#endif
}

void FNexusLinkEditorModule::ShutdownModule()
{
#if NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
#else
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
#endif

	if (GIsEditor && FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UNexusLinkSettings::StaticClass()->GetFName());
	}
}

void FNexusLinkEditorModule::OnPostEngineInit()
{
	// 每会话启动时静默检查一次版本更新；仅当有新版本时弹出非阻塞通知（需 Editor UI）
	static bool bVersionChecked = false;
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!GIsEditor || bVersionChecked || !Settings->bCheckUpdateOnStartup)
	{
		return;
	}
	bVersionChecked = true;
	CallNextTick([]()
	{
		FNexusUpdateChecker::CheckAsync(
			[](bool bHasUpdate, FString LatestVersion, FString CurrentVersion)
			{
				if (!bHasUpdate)
				{
					return;
				}
				FNotificationInfo Info(FText::FromString(
					FString::Printf(TEXT("NexusLink 有新版本可用：%s（当前 %s）"),
						*LatestVersion, *CurrentVersion)));
				Info.bFireAndForget = true;
				Info.ExpireDuration = 10.0f;
				Info.bUseSuccessFailIcons = true;
				Info.bUseLargeFont = false;
				Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
				{
					FPlatformProcess::LaunchURL(
						TEXT("https://github.com/bytepine/NexusLink/releases"),
						nullptr, nullptr);
				});
				Info.HyperlinkText = LOCTEXT("UpdateNotifLink", "查看 Releases 页面");
				FSlateNotificationManager::Get().AddNotification(Info)
					->SetCompletionState(SNotificationItem::CS_Success);
			}
		);
	});
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNexusLinkEditorModule, NexusLinkEditor)
