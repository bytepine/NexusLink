// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusRuntimeMultiSectionCapability.h"

/**
 * get_actor_animation —— 查询单个 Actor 上 AnimInstance 的运行时状态/插槽/变量。
 * 继承 FNexusMultiSectionCapability，Sections=["State","Slots","Variables"]。
 * 默认 section：state。单目标 ActorName；跨目标用 CallCapability.calls[]。
 */
class FNexusGetRuntimeActorAnimationCapability : public FNexusRuntimeMultiSectionCapability
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
};
