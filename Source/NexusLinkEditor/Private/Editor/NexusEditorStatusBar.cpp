// Copyright byteyang. All Rights Reserved.

#include "Editor/NexusEditorStatusBar.h"

#if WITH_EDITOR

#include "Utils/NexusVersionCompat.h"
#include "NexusLinkSettings.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

static const FName GNexusMcpStatusId(TEXT("NexusLink.MCP"));
static const FName GNexusWsStatusId(TEXT("NexusLink.WS"));

static TAttribute<EVisibility> MakePortVisibility()
{
	return MakeAttributeLambda([]()
	{
		const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
		return (Settings && Settings->bShowPort) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	});
}

void FNexusEditorStatusBar::Register(int32 McpPort, int32 WsPort)
{
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

#if NX_UE_HAS_LEVEL_EDITOR_TITLE_BAR_ITEM
	FLevelEditorModule::FTitleBarItem McpItem;
	FLevelEditorModule::FTitleBarItem WsItem;
#else
	FLevelEditorModule::FStatusBarItem McpItem;
	FLevelEditorModule::FStatusBarItem WsItem;
#endif

	const TAttribute<EVisibility> Visibility = MakePortVisibility();

	McpItem.Visibility = Visibility;
	McpItem.Label = FText::FromString(TEXT("MCP: "));
	McpItem.Value = FText::FromString(FString::FromInt(McpPort));

	WsItem.Visibility = Visibility;
	WsItem.Label = FText::FromString(TEXT("WS: "));
	WsItem.Value = FText::FromString(FString::FromInt(WsPort));

#if NX_UE_HAS_LEVEL_EDITOR_TITLE_BAR_ITEM
	LevelEditor.AddTitleBarItem(GNexusMcpStatusId, McpItem);
	LevelEditor.AddTitleBarItem(GNexusWsStatusId, WsItem);
#else
	LevelEditor.AddStatusBarItem(GNexusMcpStatusId, McpItem);
	LevelEditor.AddStatusBarItem(GNexusWsStatusId, WsItem);
#endif
}

void FNexusEditorStatusBar::Unregister()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		return;
	}

	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
#if NX_UE_HAS_LEVEL_EDITOR_TITLE_BAR_ITEM
	LevelEditor.RemoveTitleBarItem(GNexusMcpStatusId);
	LevelEditor.RemoveTitleBarItem(GNexusWsStatusId);
#else
	LevelEditor.RemoveStatusBarItem(GNexusMcpStatusId);
	LevelEditor.RemoveStatusBarItem(GNexusWsStatusId);
#endif
}

#endif // WITH_EDITOR
