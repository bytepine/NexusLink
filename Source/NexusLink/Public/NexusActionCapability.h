// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

using FNexusActionHandler = TFunction<void(const TSharedPtr<FJsonObject>& Op, struct FNexusActionContext&)>;

/** manage_* 单目标批量 operations[] 的执行上下文。 */
struct FNexusActionContext
{
	TSharedPtr<FJsonObject> Args;
	TSharedPtr<FJsonObject> Entry;
	FString AssetPath;
	FString Action;
	void* Target = nullptr;
	/** 指向 ResultBuilder 内 OutEntries，仅 handler 执行期有效。 */
	TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
};

/**
 * Action-dispatch Capability 基类。
 *
 * 子类实现 RegisterActions() 注册 action→handler 映射；
 * 可选 override PrepareTarget / FinalizeTarget / AfterPrepareTarget。
 * Execute 由基类 final 实现：ExtractOperations → PrepareTarget → 逐 op 分派 → FinalizeTarget。
 */
class NEXUSLINK_API FNexusActionCapability : public FNexusCapability
{
protected:
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const = 0;

	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override final;

	/** 加载 assetPath 对应目标，写 locator 到 Entry；失败时填 OutError 或 Entry.error。 */
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const;

	virtual void FinalizeTarget(void* Target) const {}

	/** PrepareTarget 成功后、执行 operations 前（如 BT 关编辑器 Tab 写 TopFields）。 */
	virtual void AfterPrepareTarget(void* Target, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutTop) const {}
};
