// Copyright byteyang. All Rights Reserved.

#include "Editor/NexusEditorStatusBar.h"

#if WITH_EDITOR

#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPortUtils.h"
#include "NexusLinkSettings.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "IAssetViewport.h"
#include "SLevelViewport.h"
#include "Styling/CoreStyle.h"

#if NX_UE_HAS_APP_STYLE
	#include "Styling/AppStyle.h"
	#define NEXUS_STYLE FAppStyle::Get()
#else
	#include "EditorStyleSet.h"
	#define NEXUS_STYLE FEditorStyle::Get()
#endif

TSharedPtr<FExtender> FNexusEditorStatusBar::Register(int32 McpPort, int32 WsPort)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		return nullptr;
	}

	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();

	// 将工具栏组件追加在 Settings 分组之后
	Extender->AddToolBarExtension(
		TEXT("Settings"),
		EExtensionHook::After,
		nullptr,
		FToolBarExtensionDelegate::CreateLambda([McpPort, WsPort](FToolBarBuilder& Builder)
		{
		Builder.AddSeparator();
			Builder.AddWidget(
				SNew(SBox)
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.0f, 0.0f))
				.Visibility(MakeAttributeLambda([]()
				{
					const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
					return (Settings && Settings->bShowPort) ? EVisibility::Visible : EVisibility::Collapsed;
				}))
				[
					SNew(SButton)
					.ButtonStyle(&NEXUS_STYLE.GetWidgetStyle<FButtonStyle>(TEXT("NoBorder")))
					.ToolTipText(FText::FromString(FString::Printf(
						TEXT("NexusLink 运行中\nMCP  端口：%d\nWebSocket 端口：%d\n\n点击打开设置"),
						McpPort, WsPort)))
					.OnClicked(FOnClicked::CreateLambda([]() -> FReply
					{
						FNexusPortUtils::OpenSettingsPanel();
						return FReply::Handled();
					}))
					.ContentPadding(FMargin(4.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("⬢ NexusLink  MCP:%d  WS:%d"), McpPort, WsPort)))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.85f, 0.45f, 1.0f)))
					]
				]
			);
		})
	);

	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditor.GetToolBarExtensibilityManager()->AddExtender(Extender);

	return Extender;
}

void FNexusEditorStatusBar::Unregister(TSharedPtr<FExtender> Extender)
{
	if (!Extender.IsValid() || !FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		return;
	}
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditor.GetToolBarExtensibilityManager()->RemoveExtender(Extender);
}

// 当前注册的视口弱引用和覆盖层组件引用，用于关闭时清理
static TWeakPtr<SLevelViewport> GRegisteredViewport;
static TSharedPtr<SWidget>      GViewportOverlayWidget;

void FNexusEditorStatusBar::RegisterViewportStats(int32 McpPort, int32 WsPort)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		return;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport();
	if (!ActiveViewport.IsValid())
	{
		return;
	}

	TSharedPtr<SLevelViewport> LevelViewport = StaticCastSharedPtr<SLevelViewport>(ActiveViewport);
	if (!LevelViewport.IsValid())
	{
		return;
	}

	// 在视口右上角添加端口文本（位于"显示帧率和内存"同区域）
	// SBox 在 SOverlay 全填充槽中撑满视口，须用 HitTestInvisible 避免遮挡视口点击
	GViewportOverlayWidget =
		SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 4.0f, 8.0f, 0.0f))
		.Visibility(MakeAttributeLambda([]()
		{
			const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
			return (Settings && Settings->bShowPort) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		}))
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("NexusLink  MCP:%d  WS:%d"), McpPort, WsPort)))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.85f, 0.45f, 1.0f)))
			.ShadowColorAndOpacity(FLinearColor::Black)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
		];

	LevelViewport->AddOverlayWidget(GViewportOverlayWidget.ToSharedRef());
	GRegisteredViewport = LevelViewport;
}

void FNexusEditorStatusBar::UnregisterViewportStats()
{
	if (GViewportOverlayWidget.IsValid())
	{
		if (TSharedPtr<SLevelViewport> VP = GRegisteredViewport.Pin())
		{
			VP->RemoveOverlayWidget(GViewportOverlayWidget.ToSharedRef());
		}
		GViewportOverlayWidget.Reset();
		GRegisteredViewport.Reset();
	}
}

#endif // WITH_EDITOR
