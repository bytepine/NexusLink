// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** get_asset_material_parameter_collection — 列举 MPC 的标量/向量参数及默认值。 */
class FGetAssetMaterialParameterCollectionCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
