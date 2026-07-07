// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** create_asset_material_parameter_collection — 创建空白 UMaterialParameterCollection。 */
class FCreateAssetMaterialParameterCollectionCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
