// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** GetAssetStruct — UserDefinedStruct 字段列表（name/type/SubType/SubCategoryObject/DefaultValue/guid）；PropertyPaths 过滤字段名。 */
class FNexusGetAssetStructCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
