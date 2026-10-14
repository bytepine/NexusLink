// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_METASOUND

/** GetAssetMetaSound — 读取 MetaSound Source 的 inputs/outputs/nodes 概览。 */
class FNexusGetAssetMetaSoundCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_METASOUND
