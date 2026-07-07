// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_APP_STYLE

#include "NexusCapability.h"

/** get_asset_eqs — 读取 UEnvQuery 的 Option/Generator/Test 概览（UE5+）。 */
class FGetAssetEQSCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // NX_UE_HAS_APP_STYLE
