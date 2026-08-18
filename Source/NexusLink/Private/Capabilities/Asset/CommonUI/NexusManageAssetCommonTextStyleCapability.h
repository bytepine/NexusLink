// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_COMMON_UI
#include "NexusActionCapability.h"
class FManageAssetCommonTextStyleCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};
#endif
