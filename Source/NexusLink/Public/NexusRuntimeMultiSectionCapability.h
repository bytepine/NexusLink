// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"

/**
 * 运行时 Multi-section Capability 基类——宿主可见性同 FNexusRuntimeCapability。
 */
class NEXUSLINK_API FNexusRuntimeMultiSectionCapability : public FNexusMultiSectionCapability
{
public:
	virtual ENexusCapabilityHostScope GetHostScope() const override
	{
		return ENexusCapabilityHostScope::Runtime;
	}
};
