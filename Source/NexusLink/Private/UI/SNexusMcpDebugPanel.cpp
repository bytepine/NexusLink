// Copyright byteyang. All Rights Reserved.

#include "UI/SNexusMcpDebugPanel.h"

#if NEXUSLINK_WITH_SERVER

#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLink.h"
#include "NexusLinkSettings.h"
#include "NexusMcpActivation.h"
#include "NexusMcpTool.h"
#include "Server/NexusMcpServer.h"
#include "Utils/NexusHostUtils.h"

#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "NexusLinkMcpPanel"

/** 与 FNexusLinkModule::HandleMcpCommand 中的同名 static 重复一份小映射，避免为一行文案改公共头。 */
static const TCHAR* McpSourceToDisplayString(ENexusMcpSource Source)
{
	switch (Source)
	{
		case ENexusMcpSource::Preferences: return TEXT("Preferences");
		case ENexusMcpSource::CommandLine: return TEXT("启动参数");
		case ENexusMcpSource::Console:     return TEXT("控制台");
		default:                           return TEXT("未知");
	}
}

void SNexusMcpDebugPanel::Construct(const FArguments& InArgs)
{
	OnRequestClose = InArgs._OnRequestClose;

	RebuildCapabilityGroups();
	CapabilityListBox = SNew(SVerticalBox);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(10.f))
		[
			SNew(SBox)
			.WidthOverride(600.f)
			.HeightOverride(680.f)
			[
				SNew(SVerticalBox)

				// 标题栏
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Title", "NexusLink MCP 调试面板"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("Close", "关闭 (Esc)"))
						.OnClicked(this, &SNexusMcpDebugPanel::OnClickClose)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SSeparator)
				]

				// MCP 状态区：只读信息 + on/off/restart（与控制台命令同一套 FNexusMcpActivation）
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(this, &SNexusMcpDebugPanel::GetMcpStatusText)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("McpOn", "开启"))
							.OnClicked(this, &SNexusMcpDebugPanel::OnClickMcpOn)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("McpOff", "关闭"))
							.OnClicked(this, &SNexusMcpDebugPanel::OnClickMcpOff)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("McpRestart", "重启"))
							.OnClicked(this, &SNexusMcpDebugPanel::OnClickMcpRestart)
						]
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SSeparator)
				]

				// Capability 区：过滤 + 清除会话覆盖
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("FilterHint", "过滤 Capability（名称 / 描述）"))
						.OnTextChanged(this, &SNexusMcpDebugPanel::OnFilterTextChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearOverrides", "清除本会话覆盖"))
						.ToolTipText(LOCTEXT("ClearOverridesTip", "恢复到 Preferences 持久配置状态（不影响 ini）"))
						.OnClicked(this, &SNexusMcpDebugPanel::OnClickClearSessionOverrides)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
					.Text(this, &SNexusMcpDebugPanel::GetSessionOverrideSummaryText)
				]

				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						CapabilityListBox.ToSharedRef()
					]
				]
			]
		]
	];

	RebuildCapabilityListWidgets();
}

FReply SNexusMcpDebugPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnRequestClose.ExecuteIfBound();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SNexusMcpDebugPanel::OnClickClose()
{
	OnRequestClose.ExecuteIfBound();
	return FReply::Handled();
}

FText SNexusMcpDebugPanel::GetMcpStatusText() const
{
	FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
	const TSharedPtr<FNexusMcpServer>& Server = Module.GetMcpServer();

	if (Server.IsValid() && Server->IsRunning())
	{
		ENexusMcpSource Source;
		FString Reason;
		FNexusMcpActivation::IsRequested(Source, Reason);

		const TCHAR* BindAddr = Server->IsLanBound() ? TEXT("0.0.0.0") : TEXT("127.0.0.1");
		return FText::FromString(FString::Printf(
			TEXT("状态：运行中（来源：%s）\nHTTP: http://%s:%d/stream\nWS: ws://%s:%d/\n鉴权：%s"),
			McpSourceToDisplayString(Source),
			BindAddr, Server->GetMcpPort(),
			BindAddr, Server->GetWsPort(),
			UNexusLinkSettings::IsMcpAuthRequired() ? TEXT("开") : TEXT("关")));
	}

	ENexusMcpSource Source;
	FString Reason;
	FNexusMcpActivation::IsRequested(Source, Reason);
	return FText::FromString(FString::Printf(TEXT("状态：未运行\n原因：%s"), *Reason));
}

FReply SNexusMcpDebugPanel::OnClickMcpOn()
{
	FNexusMcpActivation::SetConsoleEnable(true);
	FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink")).ApplyDesiredMcpState();
	return FReply::Handled();
}

FReply SNexusMcpDebugPanel::OnClickMcpOff()
{
	// 与控制台 off 一致：稳定压制，避免后续 Preferences 改动把这里关掉的服务重新拉起
	FNexusMcpActivation::ClearConsoleOverrides();
	FNexusMcpActivation::SetConsoleEnable(false);
	FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink")).ApplyDesiredMcpState();
	return FReply::Handled();
}

FReply SNexusMcpDebugPanel::OnClickMcpRestart()
{
	FNexusLinkModule& Module = FModuleManager::GetModuleChecked<FNexusLinkModule>(TEXT("NexusLink"));
	Module.StopMcpServer();
	Module.ApplyDesiredMcpState();
	return FReply::Handled();
}

void SNexusMcpDebugPanel::RebuildCapabilityGroups()
{
	Groups.Reset();

	// 先按 GroupName 聚到 TMap，再一次性搬进 Groups；避免边扫描边往 TArray 里加分组导致
	// TArray 增长重分配使之前保存的组内指针失效。
	TMap<FString, TArray<FCapRow>> GroupMap;

	const bool bFullEditorHost = FNexusHostUtils::IsFullEditorCapabilityHost();
	for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		if (!FNexusHostUtils::IsCapabilityVisibleOnHost(Record, bFullEditorHost))
		{
			continue;
		}
		const FString GroupName = Record.SourceRelDir.IsEmpty() ? TEXT("其他") : Record.SourceRelDir;
		GroupMap.FindOrAdd(GroupName).Add(FCapRow{ &Record });
	}

	GroupMap.KeySort(TLess<FString>());
	for (TPair<FString, TArray<FCapRow>>& Pair : GroupMap)
	{
		Pair.Value.Sort([](const FCapRow& A, const FCapRow& B)
		{
			return A.Record->Def.Name < B.Record->Def.Name;
		});
		Groups.Add(FCapGroup{ Pair.Key, MoveTemp(Pair.Value) });
	}
}

void SNexusMcpDebugPanel::RebuildCapabilityListWidgets()
{
	if (!CapabilityListBox.IsValid())
	{
		return;
	}
	CapabilityListBox->ClearChildren();

	const FString Needle = FilterText.TrimStartAndEnd();
	for (const FCapGroup& Group : Groups)
	{
		if (Needle.IsEmpty())
		{
			CapabilityListBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				BuildCapabilityGroupWidget(Group)
			];
			continue;
		}

		FCapGroup Filtered;
		Filtered.GroupName = Group.GroupName;
		for (const FCapRow& Row : Group.Rows)
		{
			if (Row.Record->Def.Name.Contains(Needle, ESearchCase::IgnoreCase)
				|| Row.Record->Def.Description.Contains(Needle, ESearchCase::IgnoreCase))
			{
				Filtered.Rows.Add(Row);
			}
		}
		if (Filtered.Rows.Num() > 0)
		{
			CapabilityListBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				BuildCapabilityGroupWidget(Filtered)
			];
		}
	}
}

TSharedRef<SWidget> SNexusMcpDebugPanel::BuildCapabilityGroupWidget(const FCapGroup& Group)
{
	const TSharedRef<SVerticalBox> RowsBox = SNew(SVerticalBox);
	for (const FCapRow& Row : Group.Rows)
	{
		RowsBox->AddSlot().AutoHeight().Padding(FMargin(12.f, 0.f, 0.f, 0.f))
		[
			BuildCapabilityRowWidget(*Row.Record)
		];
	}

	return SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.AreaTitle(FText::FromString(FString::Printf(TEXT("%s（%d）"), *Group.GroupName, Group.Rows.Num())))
		.BodyContent()
		[
			RowsBox
		];
}

TSharedRef<SWidget> SNexusMcpDebugPanel::BuildCapabilityRowWidget(const FCapRecord& Record)
{
	const FString CapName    = Record.Def.Name;
	const bool    bDangerous = Record.Def.HasTag(FNexusMcpTags::Dangerous);
	const bool    bReadonly  = Record.Def.HasTag(FNexusMcpTags::Readonly);
	const bool    bRuntime   = Record.Instance->GetHostScope() == ENexusCapabilityHostScope::Runtime;

	FString Badges = bDangerous ? TEXT("[危险]") : TEXT("");
	Badges += bReadonly ? TEXT("[只读]") : TEXT("[写]");
	Badges += bRuntime ? TEXT("[Runtime]") : TEXT("[Editor]");

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 1.f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SNexusMcpDebugPanel::GetCapCheckState, CapName)
			.OnCheckStateChanged(this, &SNexusMcpDebugPanel::OnCapCheckStateChanged, CapName)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(CapName))
			.ColorAndOpacity(bDangerous ? FSlateColor(FLinearColor(1.f, 0.35f, 0.3f)) : FSlateColor::UseForeground())
			.ToolTipText(FText::FromString(Record.Def.Description))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Badges))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		];
}

void SNexusMcpDebugPanel::OnFilterTextChanged(const FText& NewText)
{
	FilterText = NewText.ToString();
	RebuildCapabilityListWidgets();
}

ECheckBoxState SNexusMcpDebugPanel::GetCapCheckState(FString CapName) const
{
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	return (Settings && Settings->IsCapabilityEnabled(CapName)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNexusMcpDebugPanel::OnCapCheckStateChanged(ECheckBoxState NewState, FString CapName)
{
	if (UNexusLinkSettings* Settings = UNexusLinkSettings::Get())
	{
		Settings->SetSessionCapabilityEnabled(CapName, NewState == ECheckBoxState::Checked);
	}
}

FReply SNexusMcpDebugPanel::OnClickClearSessionOverrides()
{
	if (UNexusLinkSettings* Settings = UNexusLinkSettings::Get())
	{
		Settings->ClearSessionCapabilityOverrides();
	}
	return FReply::Handled();
}

FText SNexusMcpDebugPanel::GetSessionOverrideSummaryText() const
{
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();

	int32 Total   = 0;
	int32 Enabled = 0;
	for (const FCapGroup& Group : Groups)
	{
		Total += Group.Rows.Num();
		if (!Settings)
		{
			continue;
		}
		for (const FCapRow& Row : Group.Rows)
		{
			if (Settings->IsCapabilityEnabled(Row.Record->Def.Name))
			{
				++Enabled;
			}
		}
	}

	const int32 OverrideCount = Settings
		? (Settings->SessionEnabledCapabilities.Num() + Settings->SessionDisabledCapabilities.Num())
		: 0;

	return FText::FromString(FString::Printf(
		TEXT("启用 %d / 共 %d（本会话覆盖 %d 项；宿主：%s）"),
		Enabled, Total, OverrideCount,
		FNexusHostUtils::IsFullEditorCapabilityHost() ? TEXT("Editor") : TEXT("Runtime")));
}

#undef LOCTEXT_NAMESPACE

#endif // NEXUSLINK_WITH_SERVER
