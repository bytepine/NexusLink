// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_ENHANCED_INPUT

#include "NexusCapability.h"

/** manage_asset_input_mapping_context — 添加/移除/修改 IMC 的 Action-Key 绑定（UE5+）。 */
class FManageAssetInputMappingContextCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_ENHANCED_INPUT
