// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** manage_asset_material_parameter_collection — 增删改 MPC 的标量/向量参数。 */
class FManageAssetMaterialParameterCollectionCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
