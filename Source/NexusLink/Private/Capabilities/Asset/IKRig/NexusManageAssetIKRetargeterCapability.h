// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusActionCapability.h"

#if WITH_IK_RIG

/** manage_asset_ik_retargeter — 设置源/目标 IKRig、更新 Chain Mapping。 */
class FManageAssetIKRetargeterCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void AfterPrepareTarget(void* Target, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutTop) const override;
	virtual void FinalizeTarget(void* Target) const override;
};

#endif // WITH_IK_RIG
