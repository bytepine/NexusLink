// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * get_asset_data_asset：返回 DataAsset 的类名与 CPF_Edit 属性列表（名称、类型、当前值、是否继承）。
 */
class FGetAssetDataAssetCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
