// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UDataAsset;

/** manage_asset_data_asset — 批量 set（校验 ImportText）/ reset（从类 CDO 拷贝）；ops 至少一项；成功后统一 MarkPackageDirty。 */
class FManageAssetDataAssetCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
