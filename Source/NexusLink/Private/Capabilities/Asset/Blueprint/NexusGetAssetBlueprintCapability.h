// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"
#include "Utils/NexusAssetUtils.h"

/**
 * get_asset 的 Blueprint 子能力：返回变量 / 函数 / 组件层级 / Graph 节点 / Defaults。
 * 继承 FNexusMultiSectionCapability，sections=["variable","function","component","graph","graphOverview","defaults"]。
 * 默认 section 列表：variable + function（行为与旧版未传 section 一致）。
 * component section：合并本 BP 自有 SCS + 父蓝图链继承 SCS + C++ 原生组件，输出扁平列表（components）
 * + 层级树（hierarchy），每条附 source（owned/inherited/native）与 inherited 布尔标记。
 */
class FGetAssetBlueprintCapability : public FNexusMultiSectionCapability
{
protected:
	// ── FNexusCapability 基础钩子 ──
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;

	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
	// ── multi-section 钩子 ──
	virtual TArray<FString> GetSectionNames() const override;
	virtual TArray<FString> GetDefaultSectionNames() const override;
	virtual bool PrepareEntry(const TSharedPtr<FJsonObject>& Args,
	                          TSharedPtr<FJsonObject>&       OutEntry,
	                          void*&                         OutTargetOpaque,
	                          FString&                       OutError) const override;
	virtual void ExecuteSection(const FString&                 SectionName,
	                            const TSharedPtr<FJsonObject>& Args,
	                            void*                          TargetOpaque,
	                            TSharedPtr<FJsonObject>&       InOutDetail,
	                            FString&                       OutError) const override;
	virtual TArray<TSharedPtr<FJsonObject>> ExpandPerEntry(const TSharedPtr<FJsonObject>& Args) const override;
};
