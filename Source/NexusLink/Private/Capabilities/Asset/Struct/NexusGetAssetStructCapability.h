// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** get_asset_struct — UserDefinedStruct 字段列表（name/type/subType/subCategoryObject/defaultValue/guid）；propertyPaths 过滤字段名。 */
class FGetAssetStructCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
