// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_IK_RIG
#include "NexusCapability.h"

/** CreateAssetIKRetargeter：创建空白 IKRetargeter。 */
class FNexusCreateAssetIKRetargeterCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
