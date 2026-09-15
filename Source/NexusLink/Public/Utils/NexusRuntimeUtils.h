// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Runtime
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;
class UWidget;
class UUserWidget;
class UWorld;

/** Runtime 工具通用辅助函数集（World 获取、Actor/Widget 查找、属性路径解析）。 */
class NEXUSLINK_API FNexusRuntimeUtils
{
public:
	/**
	 * 当前活跃 World：PIE 优先，否则第一个 Game 或 Editor World。
	 * 只读查询若需在未开 PIE 时看编辑器关卡，用本函数。写路径请用 RequirePlayWorld。
	 */
	static UWorld*               GetActiveWorld();

	/**
	 * 只要 PIE 或 Game World。编辑器空闲（仅 Editor World）时返回 nullptr。
	 * 失败时填写 OutError。Runtime 写 cap（spawn/destroy/set/interact）必须走这里，
	 * 避免未开 PIE 时改到关卡里的 Actor。
	 */
	static UWorld* RequirePlayWorld(FString& OutError);
	/** 编辑器下返回 ActorLabel，非编辑器回退 GetName()。 */
	static FString               GetActorLabelOrName(const AActor* Actor);
	static AActor*               FindActorByName(UWorld* World, const FString& ActorName);
	static FString               GetWidgetDisplayText(UWidget* Widget);
	static TArray<UUserWidget*>  GetActiveUserWidgets();
	static UWidget*              FindRuntimeWidget(const FString& WidgetClassName, const FString& WidgetName);

	/**
	 * 递归遍历 UserWidget 下所有子控件（走面板层级而非 WidgetTree 注册列表）。
	 * 与 UWidgetTree::ForEachWidget 的区别：能发现运行时通过 AddChild() 动态添加的控件。
	 */
	static void ForEachWidgetRecursive(UUserWidget* UserWidget, const TFunction<void(UWidget*)>& Callback);

	/**
	 * 解析 Actor 上单条属性路径（支持 "CompName.PropA.PropB" 组件前缀语法）。
	 * 组件名首段自动路由到对应组件；仅组件名时返回组件属性列表。
	 */
	static bool ResolveActorPropertyPath(
		AActor* Actor,
		const FString& PropertyPath,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError);
};
