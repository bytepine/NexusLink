// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusActionCapability.h"
#if WITH_NEXUS_TAGS_EDITOR
class FNexusManageGameplayTagsCapability : public FNexusActionCapability
{
protected:
    void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
    bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& Error) const override;
    void FinalizeTarget(void* Target) const override;
    void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
};
#endif
