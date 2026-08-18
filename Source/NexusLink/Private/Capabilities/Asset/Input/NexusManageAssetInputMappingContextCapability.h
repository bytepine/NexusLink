// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_ENHANCED_INPUT

#include "NexusActionCapability.h"

/** manage_asset_input_mapping_context — 添加/移除/修改 IMC 的 Action-Key 绑定（UE5+）。 */
class FManageAssetInputMappingContextCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};

#endif // WITH_ENHANCED_INPUT
