// Copyright byteyang. All Rights Reserved.

#include "NexusLinkSettings.h"
#include "Editor/NexusLogCapture.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLink.h"
#include "Server/NexusMcpServer.h"

UNexusLinkSettings::UNexusLinkSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName  = TEXT("NexusLink");
}

UNexusLinkSettings* UNexusLinkSettings::Get()
{
	return GetMutableDefault<UNexusLinkSettings>();
}

FName UNexusLinkSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

bool UNexusLinkSettings::IsCapabilityEnabled(const FString& CapabilityName) const
{
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

#if WITH_EDITOR
FText UNexusLinkSettings::GetSectionText() const
{
	return NSLOCTEXT("NexusLink", "SettingsSectionText", "NexusLink");
}

void UNexusLinkSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedProp = PropertyChangedEvent.GetPropertyName();

	// 白名单变更时实时同步给日志捕获器（无需重启编辑器立即生效）
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
}
#endif
