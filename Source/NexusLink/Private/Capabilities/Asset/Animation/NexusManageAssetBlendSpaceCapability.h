// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusActionCapability.h"

/** manage_asset_blend_space — 编辑 BlendSpace 轴参数与样本。 */
class FManageAssetBlendSpaceCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void AfterPrepareTarget(void* Target, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutTop) const override;
	virtual void FinalizeTarget(void* Target) const override;
};
