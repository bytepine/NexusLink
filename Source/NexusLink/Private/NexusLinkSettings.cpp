// Copyright byteyang. All Rights Reserved.

#include "NexusLinkSettings.h"
#include "Editor/NexusLogCapture.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLink.h"
#include "NexusMcpAuth.h"
#include "Server/NexusMcpServer.h"
#if WITH_EDITOR
#include "Misc/MessageDialog.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNexusLinkSettings, Log, All);

UNexusLinkSettings::UNexusLinkSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName  = TEXT("NexusLink");
}

UNexusLinkSettings* UNexusLinkSettings::Get()
{
	return GetMutableDefault<UNexusLinkSettings>();
}

FString UNexusLinkSettings::GetExtraMcpAuthTokensText() const
{
	FString Out;
	for (int32 i = 0; i < ExtraMcpAuthTokens.Num(); ++i)
	{
		if (i > 0)
		{
			Out += TEXT("\n");
		}
		Out += ExtraMcpAuthTokens[i];
	}
	return Out;
}

void UNexusLinkSettings::PostInitProperties()
{
	Super::PostInitProperties();
	// 旧版本这里是逗号分隔的单个字符串，升级后按数组逐条归一化
	TArray<FString> Normalized;
	for (const FString& Item : ExtraMcpAuthTokens)
	{
		FNexusMcpAuth::ParseAuthTokens(Item, Normalized);
	}
	if (Normalized.Num() == 0 && ExtraMcpAuthTokens.Num() > 0)
	{
		// 全部解析不出合法 token 时保留原样并提示，避免把用户填的内容静默清空落盘
		UE_LOG(LogNexusLinkSettings, Warning,
			TEXT("额外鉴权 Token 中没有合法条目（须为 32-128 位十六进制），已保留原值未改写配置"));
		return;
	}
	if (Normalized != ExtraMcpAuthTokens)
	{
		ExtraMcpAuthTokens = MoveTemp(Normalized);
		SaveConfig();
	}
}

bool UNexusLinkSettings::IsMcpAuthRequired()
{
	const UNexusLinkSettings* Settings = Get();
	return !Settings || Settings->bRequireMcpAuth;
}

FName UNexusLinkSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

bool UNexusLinkSettings::IsCapabilityEnabled(const FString& CapabilityName) const
{
	if (SessionEnabledCapabilities.Contains(CapabilityName))
	{
		return true;
	}
	return !DisabledCapabilities.Contains(CapabilityName);
}

void UNexusLinkSettings::SetCapabilityEnabled(const FString& CapabilityName, bool bEnabled, bool bNotify)
{
	if (bEnabled)
	{
		DisabledCapabilities.Remove(CapabilityName);
	}
	else
	{
		DisabledCapabilities.Add(CapabilityName);
		// 用户显式关闭时同时撤掉会话级强制启用，否则本次会话内关不掉
		SessionEnabledCapabilities.Remove(CapabilityName);
	}

	if (bNotify)
	{
		NotifyCapabilitiesChanged();
	}
}

void UNexusLinkSettings::NotifyCapabilitiesChanged()
{
	SaveConfig();

	// MultiTool 模式下 Capability 启用/禁用会影响工具列表，需广播通知
	if (ToolsListMode == ENexusToolsListMode::MultiTool)
	{
		FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
		const TSharedPtr<FNexusMcpServer>& Server = Module.GetMcpServer();
		if (Server.IsValid() && Server->IsRunning())
		{
			Server->BroadcastNotification(TEXT("notifications/tools/list_changed"));
		}
	}
}

void UNexusLinkSettings::EnsureDefaultCapabilityMode()
{
	bool bChanged = false;

	for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		const FString& Name = Record.Def.Name;
		if (!KnownCapabilityKeys.Contains(Name))
		{
			KnownCapabilityKeys.Add(Name);
			bChanged = true;
		}
	}

	bCapabilityDefaultsApplied = true;

	if (bChanged)
	{
		SaveConfig();
	}
}

static const TCHAR* GDangerousCapabilityNames[] = {
	TEXT("exec_command"),
	TEXT("eval_runtime_lua"),
	TEXT("dofile_runtime_lua"),
};

void UNexusLinkSettings::EnsureDangerousCapsDefaultOff()
{
	if (bDangerousCapsDefaultOffApplied)
	{
		return;
	}
	bDangerousCapsDefaultOffApplied = true;
	for (const TCHAR* Name : GDangerousCapabilityNames)
	{
		DisabledCapabilities.Add(Name);
	}
	SaveConfig();
}

void UNexusLinkSettings::EnableDangerousCapsForSession()
{
	// 只写会话级集合：DisabledCapabilities 一旦被改，后续任意一次 SaveConfig
	// （EnsureLogCaptureDefaults、设置面板勾选等）都会把危险 cap 永久写成启用
	for (const TCHAR* Name : GDangerousCapabilityNames)
	{
		SessionEnabledCapabilities.Add(Name);
	}
}

bool UNexusLinkSettings::EnsureLogCaptureDefaults()
{
	if (bLogCaptureDefaultsApplied)
	{
		return false;
	}

	bLogCaptureDefaultsApplied = true;
	bool bWroteDefaults = false;
	if (LogCaptureCategories.Num() == 0)
	{
		LogCaptureCategories = FNexusLogCapture::GetDefaultDiagnosticCategories();
		bWroteDefaults = true;
	}
	SaveConfig();
	return bWroteDefaults;
}

#if WITH_EDITOR
FText UNexusLinkSettings::GetSectionText() const
{
	return NSLOCTEXT("NexusLink", "SettingsSectionText", "NexusLink");
}

void UNexusLinkSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedProp = PropertyChangedEvent.GetPropertyName();

	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bAllowLanBind)
		|| ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bRequireMcpAuth))
	{
		if (bAllowLanBind && !bRequireMcpAuth)
		{
			const EAppReturnType::Type Ret = FMessageDialog::Open(
				EAppMsgType::YesNo,
				NSLOCTEXT("NexusLink", "LanAuthWarn",
					"局域网可达且未鉴权时，同网段主机都能控制编辑器。确定继续？不要做公网映射。"));
			if (Ret != EAppReturnType::Yes)
			{
				if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bAllowLanBind))
				{
					bAllowLanBind = false;
				}
				else
				{
					bRequireMcpAuth = true;
				}
				SaveConfig();
				return;
			}
		}
	}

	// 鉴权从关切到开：关鉴权期间连上的 WS 连接不能继续算已鉴权
	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bRequireMcpAuth) && bRequireMcpAuth)
	{
		FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
		const TSharedPtr<FNexusMcpServer>& Server = Module.GetMcpServer();
		if (Server.IsValid() && Server->IsRunning())
		{
			Server->ResetWsAuthentications();
		}
	}

	// 白名单变更时实时同步给日志捕获器（无需重启编辑器立即生效）
	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, ExtraMcpAuthTokens))
	{
		TArray<FString> Expanded;
		bool bSplit = false;
		for (const FString& Item : ExtraMcpAuthTokens)
		{
			if (Item.Contains(TEXT(",")) || Item.Contains(TEXT(";"))
				|| Item.Contains(TEXT("\n")) || Item.Contains(TEXT("\r")))
			{
				bSplit = true;
				FNexusMcpAuth::ParseAuthTokens(Item, Expanded);
			}
			else
			{
				Expanded.Add(Item);
			}
		}
		if (bSplit)
		{
			ExtraMcpAuthTokens = MoveTemp(Expanded);
		}
	}

	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, LogCaptureCategories))
	{
		FNexusLogCapture::Get().SetCategoryWhitelist(LogCaptureCategories);
	}

	// 工具列表模式变更时广播 notifications/tools/list_changed
	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, ToolsListMode))
	{
		FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
		const TSharedPtr<FNexusMcpServer>& Server = Module.GetMcpServer();
		if (Server.IsValid() && Server->IsRunning())
		{
			Server->BroadcastNotification(TEXT("notifications/tools/list_changed"));
		}
	}

	// MCP 服务器总开关：运行时即时启停，无需重启编辑器
	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bEnableMcpServer))
	{
		FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
		if (bEnableMcpServer)
		{
			Module.TryStartMcpServer();
		}
		else
		{
			Module.StopMcpServer();
		}
	}

	// 局域网绑定变更：已在跑则停再启，使 DefaultBindAddress 立即生效
	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bAllowLanBind))
	{
		FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
		const TSharedPtr<FNexusMcpServer>& Server = Module.GetMcpServer();
		if (Server.IsValid() && Server->IsRunning())
		{
			Module.StopMcpServer();
			Module.TryStartMcpServer();
		}
	}
}
#endif
