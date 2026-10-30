// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusActionCapability.h"
class FNexusManageProjectPluginCapability : public FNexusActionCapability
{
protected:
    virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
    virtual void RegisterActions(TMap<FString,FNexusActionHandler>& OutHandlers) const override;
    virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Entry,void*& Target,FString& Error) const override;
};
