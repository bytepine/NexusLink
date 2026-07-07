// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_APP_STYLE

#include "NexusCapability.h"

/** manage_asset_eqs — 编辑 EQS：add_option/remove_option/set_generator/add_test/remove_test（UE5+）。 */
class FManageAssetEQSCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // NX_UE_HAS_APP_STYLE
