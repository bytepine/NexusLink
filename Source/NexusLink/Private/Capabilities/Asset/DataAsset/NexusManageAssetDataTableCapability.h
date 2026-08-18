// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusActionCapability.h"

class UDataTable;

/** manage_asset_data_table — 批量 add/remove/set；非空 rows；ImportText 校验；add 的 fields 支持标量 JSON；有变更时单次 MarkPackageDirty。 */
class FManageAssetDataTableCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};
