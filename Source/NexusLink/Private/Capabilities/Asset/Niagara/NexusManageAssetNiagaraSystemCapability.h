// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_NIAGARA

#include "NexusCapability.h"

/** manage_asset_niagara_system — 编辑 Niagara 系统用户暴露参数。*/
class FManageAssetNiagaraSystemCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_NIAGARA
