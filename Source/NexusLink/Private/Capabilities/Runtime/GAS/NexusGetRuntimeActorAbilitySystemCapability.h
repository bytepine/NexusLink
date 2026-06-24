// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusMultiSectionCapability.h"

/**
 * get_runtime_actor_ability_system — PIE 运行时读取 Actor 的 AbilitySystemComponent 快照。
 * sections: abilities / effects / attributes。
 * 只读；不提供 Grant/Apply/Remove 写操作。
 */
class FGetRuntimeActorAbilitySystemCapability : public FNexusMultiSectionCapability
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

#endif // WITH_GAS
