// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_NIAGARA
#include "NexusCapability.h"

/** create_asset_niagara_system：创建空白 NiagaraSystem。 */
class FCreateAssetNiagaraSystemCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
