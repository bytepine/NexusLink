// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_GAS

#include "NexusActionCapability.h"

/** manage_asset_gameplay_effect — 批量修改 GE 策略/Tag/Modifier。 */
class FManageAssetGameplayEffectCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void AfterPrepareTarget(void* Target, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutTop) const override;
	virtual void FinalizeTarget(void* Target) const override;
};

#endif // WITH_GAS
