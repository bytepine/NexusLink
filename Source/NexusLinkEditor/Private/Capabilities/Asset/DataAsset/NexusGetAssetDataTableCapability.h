// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** GetAssetDataTable — mode auto|schema|rows；schema 为结构+分页行名，rows 为按 RowNames 读 fields；可选 PropertyPaths。 */
class FNexusGetAssetDataTableCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
