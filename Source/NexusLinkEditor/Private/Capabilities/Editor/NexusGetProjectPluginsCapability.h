// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusMultiSectionCapability.h"

class FNexusGetProjectPluginsCapability : public FNexusMultiSectionCapability
{
protected:
	void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
	TArray<FString> GetSectionNames() const override;
	TArray<FString> GetDefaultSectionNames() const override;
	void ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args, void* Target,
		TSharedPtr<FJsonObject>& Out, FString& Error) const override;
};
