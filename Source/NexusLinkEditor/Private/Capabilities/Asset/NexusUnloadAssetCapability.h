// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * unload_asset 工具的唯一 Capability —— 手动兜底：批量卸载已加载的包 + 可选 GC。
 * 内存高水位批量驱逐（FNexusPackageLedger）为主路径，自动运行；本 cap 用于手动强制卸载
 * 任意已加载资产（不限于 NexusLink 本次引入），排障/兜底用。
 */
class FUnloadAssetCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
