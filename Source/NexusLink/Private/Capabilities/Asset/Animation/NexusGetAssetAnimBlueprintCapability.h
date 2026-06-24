// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"

/**
 * get_asset_anim_blueprint 的 Capability — 读取 AnimBlueprint 资产的变量列表、状态机结构和 Defaults。
 * 继承 FNexusMultiSectionCapability，sections=["variables","statemachines","defaults"]。
 * 默认 section 列表：variables + statemachines。
 */
class FGetAssetAnimBlueprintCapability : public FNexusMultiSectionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;

	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
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
