// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UDataTable;

/** manage_asset_data_table — 批量 add/remove/set；非空 rows；ImportText 校验；add 的 fields 支持标量 JSON；有变更时单次 MarkPackageDirty。 */
class FManageAssetDataTableCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
