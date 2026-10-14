// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"

/**
 * GetGameplayTags 工具的唯一 Capability —— 查询 GameplayTag 层级/Actor Tag/资产 Tag。
 * 继承 FNexusMultiSectionCapability，Sections=["Hierarchy","Actor","Asset"]。
 * 默认 section：hierarchy。PrepareEntry 返回 null target；各 section 独立从 args 读参数。
 */
class FNexusGetGameplayTagsCapability : public FNexusMultiSectionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;

	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
	virtual TArray<FString> GetSectionNames() const override;
	virtual TArray<FString> GetDefaultSectionNames() const override;
	virtual void ExecuteSection(const FString&                 SectionName,
	                            const TSharedPtr<FJsonObject>& Args,
	                            void*                          TargetOpaque,
	                            TSharedPtr<FJsonObject>&       InOutDetail,
	                            FString&                       OutError) const override;
};
