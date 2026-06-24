// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "IDetailCustomization.h"
#include "Delegates/Delegate.h"
#include "Widgets/SWidget.h"

class UNexusLinkSettings;

/**
 * UNexusLinkSettings 的自定义 Detail 面板。
 * 把 DisabledCapabilities 渲染为按 Capability 源码目录（Private/Capabilities/ 多级子目录）折叠树；
 * 扫描失败时回退为按 tags 与 GetCategoryMapping 的中文分类。
 * Capability 与 Tool 已完全解耦——每个 cap 自带 tags，由注册表统一管理。
 */
class FNexusLinkSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** 分类标签 → 显示名（tag 回退分组与设置表中文标题）。 */
	static const TArray<TPair<FString, FString>>& GetCategoryMapping();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	struct FCapEntry
	{
		FString Name;
		FString Description;
		TArray<FString> Tags;
	};

	/** 设置树节点：PathKey 为相对 Capabilities 的累积路径（根节点 PathKey 为空）。 */
	struct FCapGroupNode
	{
		FString PathKey;
		TArray<TUniquePtr<FCapGroupNode>> Children;
		TArray<FCapEntry> Caps;
	};

	/** 自 PathKey 起递归构建一层 SExpandableArea（子目录嵌套于 Body 内）。 */
	TSharedRef<SWidget> CreateCapGroupWidget(FCapGroupNode* Node);

	static void SortCapGroupChildren(FCapGroupNode& Node);
	static TUniquePtr<FCapGroupNode> BuildCapGroupTree(const TMap<FString, TArray<FCapEntry>>& InCategoryCaps);
	static void ForEachCapInSubtree(FCapGroupNode& N, TFunctionRef<void(const FCapEntry&)> Fn);
	static void RefreshCapCountsRecursive(UNexusLinkSettings* Settings, FCapGroupNode* N,
		TMap<FString, TSharedPtr<class STextBlock>>& CategoryCountTexts);

	/** 刷新分类标题中的启用计数文本。 */
	void RefreshCategoryHeaders();

	/** 设置面板对象弱引用。 */
	TWeakObjectPtr<class UNexusLinkSettings> SettingsPtr;

	/** 分组 SortKey（源码相对 Capabilities 的路径，如 Asset/Blueprint；回退为 tag 字面量或 _misc）→ cap 条目（按名字典序）。 */
	TMap<FString, TArray<FCapEntry>> CategoryCaps;

	/** 由 CategoryCaps 构建的多级目录树（快捷操作与计数仍遍历扁平 CategoryCaps / 此树）。 */
	TUniquePtr<FCapGroupNode> CapTreeRoot;

	/** 分组 PathKey → 标题计数文本控件（形如 "8/11"）。 */
	TMap<FString, TSharedPtr<class STextBlock>> CategoryCountTexts;
};

#endif // WITH_EDITOR
