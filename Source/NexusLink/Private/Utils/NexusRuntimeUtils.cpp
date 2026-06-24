// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

// --- 获取活跃 World ---

UWorld* FNexusRuntimeUtils::GetActiveWorld()
{
#if WITH_EDITOR
	// PIE 优先
	if (GEditor)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
			{
				return Ctx.World();
			}
		}
	}
#endif
	// 回退到第一个 Game/Editor World
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if ((Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::Editor) && Ctx.World())
		{
			return Ctx.World();
		}
	}
	return nullptr;
}

UWorld* FNexusRuntimeUtils::RequirePlayWorld(FString& OutError)
{
	UWorld* World = GetActiveWorld();
	if (!World)
		OutError = TEXT("No active World");
	return World;
}

FString FNexusRuntimeUtils::GetActorLabelOrName(const AActor* Actor)
{
	if (!Actor) return FString();
#if WITH_EDITOR
	return Actor->GetActorLabel();
#else
	return Actor->GetName();
#endif
}

AActor* FNexusRuntimeUtils::FindActorByName(UWorld* World, const FString& ActorName)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || GetActorLabelOrName(*It) == ActorName)
		{
			return *It;
		}
	}
	return nullptr;
}

// --- Widget 辅助 ---

/**
 * 递归收集控件及其子控件中的显示文本。
 * 支持 TextBlock / RichTextBlock / EditableText / EditableTextBox 等常见文本控件，
 * 返回用 | 分隔的拼接文本。
 */
FString FNexusRuntimeUtils::GetWidgetDisplayText(UWidget* Widget)
{
	if (!Widget) return FString();

	TArray<FString> Texts;

	// 自身文本
	if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		Texts.Add(TB->GetText().ToString());
	}
	else if (URichTextBlock* RTB = Cast<URichTextBlock>(Widget))
	{
		Texts.Add(RTB->GetText().ToString());
	}
	else if (UEditableText* ET = Cast<UEditableText>(Widget))
	{
		Texts.Add(ET->GetText().ToString());
	}
	else if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Widget))
	{
		Texts.Add(ETB->GetText().ToString());
	}
	else if (UMultiLineEditableText* MET = Cast<UMultiLineEditableText>(Widget))
	{
		Texts.Add(MET->GetText().ToString());
	}

	// 递归子控件
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			FString ChildText = GetWidgetDisplayText(Panel->GetChildAt(i));
			if (!ChildText.IsEmpty()) Texts.Add(ChildText);
		}
	}

	// UserWidget 嵌套：走面板层级，覆盖运行时动态添加的子控件
	if (UUserWidget* UW = Cast<UUserWidget>(Widget))
	{
		ForEachWidgetRecursive(UW, [&](UWidget* Child)
		{
			if (Child == Widget) return;
			FString ChildText = GetWidgetDisplayText(Child);
			if (!ChildText.IsEmpty()) Texts.Add(ChildText);
		});
	}

	return FString::Join(Texts, TEXT(" | "));
}

TArray<UUserWidget*> FNexusRuntimeUtils::GetActiveUserWidgets()
{
	TArray<UUserWidget*> Result;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* W = *It;
		if (!W || !W->IsInViewport()) continue;
		Result.Add(W);
	}
	return Result;
}

UWidget* FNexusRuntimeUtils::FindRuntimeWidget(const FString& WidgetClassName, const FString& WidgetName)
{
	for (UUserWidget* UW : GetActiveUserWidgets())
	{
		if (!WidgetClassName.IsEmpty() && !UW->GetClass()->GetName().Contains(WidgetClassName))
		{
			continue;
		}

		UWidget* Found = nullptr;
		ForEachWidgetRecursive(UW, [&](UWidget* Child)
		{
			if (!Found && Child && Child->GetName() == WidgetName) Found = Child;
		});
		if (Found) return Found;
	}
	return nullptr;
}

void FNexusRuntimeUtils::ForEachWidgetRecursive(UUserWidget* UserWidget, const TFunction<void(UWidget*)>& Callback)
{
	if (!UserWidget || !UserWidget->WidgetTree || !UserWidget->WidgetTree->RootWidget)
	{
		return;
	}

	TArray<UWidget*> Stack;
	Stack.Reserve(64);
	Stack.Add(UserWidget->WidgetTree->RootWidget);

	while (Stack.Num() > 0)
	{
#if NX_UE_HAS_ALLOW_SHRINKING_ENUM
		UWidget* Current = Stack.Pop(EAllowShrinking::No);
#else
		UWidget* Current = Stack.Pop(false);
#endif
		if (!Current) continue;

		Callback(Current);

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Current))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				Stack.Add(Panel->GetChildAt(i));
			}
		}
	}
}

// --- Actor 属性路径解析 ---

bool FNexusRuntimeUtils::ResolveActorPropertyPath(
	AActor* Actor,
	const FString& PropertyPath,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0) { OutError = TEXT("propertyPath is empty"); return false; }

	UObject* Target = Actor;
	int32 StartSeg = 0;

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (Comp && Comp->GetName() == Segs[0])
		{
			if (Segs.Num() == 1)
			{
				OutResult->SetStringField(TEXT("name"), Comp->GetName());
				OutResult->SetStringField(TEXT("type"), Comp->GetClass()->GetName());
				TArray<TSharedPtr<FJsonValue>> Children;
				FNexusPropertyUtils::CollectEditableProperties(Comp, Children);
				OutResult->SetArrayField(TEXT("children"), Children);
				return true;
			}
			Target = Comp;
			StartSeg = 1;
			break;
		}
	}

	TArray<FString> Remaining;
	for (int32 i = StartSeg + 1; i < Segs.Num(); ++i) Remaining.Add(Segs[i]);
	return FNexusPropertyUtils::ResolvePropertyRead(Target, Segs[StartSeg], Remaining, OutResult, OutError);
}
