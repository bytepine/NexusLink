// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusMultiSectionCapability.h"
#if WITH_NEXUS_COMMON_UI_DEBUGGING
class FNexusGetCommonUIInfoCapability : public FNexusMultiSectionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
	virtual TArray<FString> GetSectionNames() const override;
	virtual TArray<FString> GetDefaultSectionNames() const override { return {TEXT("Players")}; }
	virtual void ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args, void* TargetOpaque, TSharedPtr<FJsonObject>& Detail, FString& Error) const override;
};
#endif
