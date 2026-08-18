// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_EQS

#include "NexusActionCapability.h"

/** manage_asset_eqs — 编辑 EQS：add_option/remove_option/set_generator/add_test/remove_test（UE5+）。 */
class FManageAssetEQSCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};

#endif // NX_UE_HAS_EQS
