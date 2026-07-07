// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_ENHANCED_INPUT

#include "NexusCapability.h"

/** manage_asset_input_action — 编辑 UInputAction（set_value_type/add_trigger/remove_trigger/add_modifier/remove_modifier/set_flags）。UE5+。 */
class FManageAssetInputActionCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_ENHANCED_INPUT
