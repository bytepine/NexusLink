// Copyright byteyang. All Rights Reserved.

#include "UI/NexusMcpDebugOverlay.h"

#if NEXUSLINK_WITH_SERVER

#include "UI/SNexusMcpDebugPanel.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SWidget.h"

TWeakPtr<SWidget>                  FNexusMcpDebugOverlay::PanelWidget;
TWeakObjectPtr<UGameViewportClient> FNexusMcpDebugOverlay::OwningViewport;
TWeakObjectPtr<APlayerController>   FNexusMcpDebugOverlay::OwningPC;
bool                                FNexusMcpDebugOverlay::bPrevShowMouseCursor = false;

bool FNexusMcpDebugOverlay::IsOpen()
{
	return PanelWidget.IsValid();
}

void FNexusMcpDebugOverlay::Open(UWorld* World)
{
	if (IsOpen())
	{
		return;
	}

	// GEngine->GameViewport 在 UE5 里是 TObjectPtr，与 World->GetGameViewport() 的裸指针三元表达式类型不一致，分两步取
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport && GEngine)
	{
		Viewport = GEngine->GameViewport;
	}
	if (!Viewport)
	{
		return;
	}

	const TSharedRef<SNexusMcpDebugPanel> Panel = SNew(SNexusMcpDebugPanel)
		.OnRequestClose(FSimpleDelegate::CreateStatic(&FNexusMcpDebugOverlay::Close));

	Viewport->AddViewportWidgetContent(Panel, /*ZOrder=*/100);
	PanelWidget    = Panel;
	OwningViewport = Viewport;

	// 独立包 / PIE 第三人称模板默认 GameOnly 且隐藏鼠标：面板要能被点击，临时切到 GameAndUI 并显示鼠标；
	// 关闭时按 bPrevShowMouseCursor 近似还原（见头文件注释，无法反查当时精确 InputMode）。
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	OwningPC = PC;
	if (PC)
	{
		bPrevShowMouseCursor = PC->bShowMouseCursor;
		PC->bShowMouseCursor = true;

		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}

	FSlateApplication::Get().SetKeyboardFocus(Panel);
}

void FNexusMcpDebugOverlay::Close()
{
	if (!IsOpen())
	{
		return;
	}

	if (UGameViewportClient* Viewport = OwningViewport.Get())
	{
		if (const TSharedPtr<SWidget> Panel = PanelWidget.Pin())
		{
			Viewport->RemoveViewportWidgetContent(Panel.ToSharedRef());
		}
	}

	RestoreInputState();

	PanelWidget.Reset();
	OwningViewport.Reset();
	OwningPC.Reset();
}

void FNexusMcpDebugOverlay::RestoreInputState()
{
	if (APlayerController* PC = OwningPC.Get())
	{
		PC->bShowMouseCursor = bPrevShowMouseCursor;
		if (bPrevShowMouseCursor)
		{
			PC->SetInputMode(FInputModeGameAndUI());
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void FNexusMcpDebugOverlay::Toggle(UWorld* World)
{
	if (IsOpen())
	{
		Close();
	}
	else
	{
		Open(World);
	}
}

#endif // NEXUSLINK_WITH_SERVER
