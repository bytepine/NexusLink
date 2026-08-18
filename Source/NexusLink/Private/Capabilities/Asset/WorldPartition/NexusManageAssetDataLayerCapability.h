// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET
#include "NexusActionCapability.h"

/** manage_asset_data_layer：修改 UDataLayerAsset 属性（≥UE5.1，WITH_EDITOR）。 */
class FManageAssetDataLayerCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};

#else
#include "NexusCapability.h"

/** DataLayerAsset 需 UE5.1+；本引擎走 stub。 */
class FManageAssetDataLayerCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif
