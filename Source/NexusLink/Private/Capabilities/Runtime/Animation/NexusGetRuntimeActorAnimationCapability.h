// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"

/**
 * get_actor_animation —— 查询单个/多个 Actor 上 AnimInstance 的运行时状态/插槽/变量。
 * 继承 FNexusMultiSectionCapability，sections=["state","slots","variables"]。
 * 默认 section：state。支持 actorNames[] 批量查询。
 */
class FGetRuntimeActorAnimationCapability : public FNexusMultiSectionCapability
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
