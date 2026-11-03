// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"

/** 使用明确工厂的单文件首次导入。 */
class FNexusImportAssetCapability : public FNexusCapability
{
protected:
	void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
