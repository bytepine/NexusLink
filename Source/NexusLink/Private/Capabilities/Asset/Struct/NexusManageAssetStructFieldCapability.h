// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UUserDefinedStruct;

/** manage_asset_struct_field 的 Capability — 批量增删/修改 UserDefinedStruct 字段（fields[]）。*/
class FManageAssetStructFieldCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
