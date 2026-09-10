// Copyright byteyang. All Rights Reserved.

#include "NexusLinkSettings.h"
#include "Editor/NexusLogCapture.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLink.h"
#include "NexusMcpAuth.h"
#include "NexusMcpTool.h"
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
	if (IsDangerousCapability(CapabilityName)
		&& DangerousCapAccess == ENexusDangerousCapAccess::Disabled)
	{
		return false;
	}
	return !DisabledCapabilities.Contains(CapabilityName);
}

bool UNexusLinkSettings::IsDangerousCapability(const FString& CapabilityName)
{
	const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(CapabilityName);
	return Rec && Rec->Def.HasTag(FNexusMcpTags::Dangerous);
}

TArray<FString> UNexusLinkSettings::CollectDangerousCapabilityNames()
{
	TArray<FString> Names;
	for (const FCapRecord& Rec : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		if (Rec.Def.HasTag(FNexusMcpTags::Dangerous))
		{
			Names.Add(Rec.Def.Name);
		}
	}
	return Names;
}

ENexusDangerousCapAccess UNexusLinkSettings::ResolveAccessAfterUpgrade(bool bAnyDangerousCurrentlyEnabled)
{
	return bAnyDangerousCurrentlyEnabled
		? ENexusDangerousCapAccess::Custom
		: ENexusDangerousCapAccess::Disabled;
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

void UNexusLinkSettings::EnsureDangerousCapsDefaultOff()
{
	const TArray<FString> DangerousNames = CollectDangerousCapabilityNames();

	// 旧版本只有一个总开关，迁移时把当时的三个 cap 视为已处理，
	// 否则用户手动启用过的 cap 会在本次升级被重新关掉
	if (bDangerousCapsDefaultOffApplied && DangerousCapsDefaultOffApplied.Num() == 0)
	{
		DangerousCapsDefaultOffApplied.Add(TEXT("exec_command"));
		DangerousCapsDefaultOffApplied.Add(TEXT("eval_runtime_lua"));
		DangerousCapsDefaultOffApplied.Add(TEXT("dofile_runtime_lua"));
	}

	bool bChanged = false;
	for (const FString& Name : DangerousNames)
	{
		if (DangerousCapsDefaultOffApplied.Contains(Name))
		{
			continue;
		}
		DangerousCapsDefaultOffApplied.Add(Name);
		if (DangerousCapAccess == ENexusDangerousCapAccess::Confirm)
		{
			DisabledCapabilities.Remove(Name);
		}
		else
		{
			DisabledCapabilities.Add(Name);
		}
		bChanged = true;
	}

	bDangerousCapsDefaultOffApplied = true;

	if (!bDangerousCapAccessMigrated)
	{
		bool bAnyEnabled = false;
		for (const FString& Name : DangerousNames)
		{
			if (!DisabledCapabilities.Contains(Name))
			{
				bAnyEnabled = true;
				break;
			}
		}
		DangerousCapAccess = ResolveAccessAfterUpgrade(bAnyEnabled);
		bDangerousCapAccessMigrated = true;
		bChanged = true;
	}

	if (DangerousCapAccess == ENexusDangerousCapAccess::Confirm && !bConfirmDangerousCapsDefaulted)
	{
		ApplyConfirmDangerousCapDefaults();
		bChanged = true;
	}

	if (bChanged)
	{
		SaveConfig();
	}
}

void UNexusLinkSettings::ApplyConfirmDangerousCapDefaults()
{
	for (const FString& Name : CollectDangerousCapabilityNames())
	{
		DisabledCapabilities.Remove(Name);
	}
	bConfirmDangerousCapsDefaulted = true;
}

void UNexusLinkSettings::EnableDangerousCapsForSession()
{
	// 只写会话级集合：DisabledCapabilities 一旦被改，后续任意一次 SaveConfig
	// （EnsureLogCaptureDefaults、设置面板勾选等）都会把危险 cap 永久写成启用
	for (const FString& Name : CollectDangerousCapabilityNames())
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
	if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, ToolsListMode)
		|| ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, DangerousCapAccess))
	{
		if (ChangedProp == GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, DangerousCapAccess))
		{
			if (DangerousCapAccess == ENexusDangerousCapAccess::Confirm)
			{
				ApplyConfirmDangerousCapDefaults();
			}
			else
			{
				bConfirmDangerousCapsDefaulted = false;
			}
			SaveConfig();
		}
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
