// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_NIAGARA

#include "NexusCapability.h"

/** get_asset_niagara_system — 只读检查 UNiagaraSystem 发射器与用户参数摘要。 */
class FGetAssetNiagaraSystemCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_NIAGARA
