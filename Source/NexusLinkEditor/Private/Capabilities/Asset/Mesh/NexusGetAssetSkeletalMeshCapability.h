// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** GetAssetSkeletalMesh — 只读检查 SkeletalMesh LOD、材质槽、骨骼与 PhysicsAsset。 */
class FNexusGetAssetSkeletalMeshCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
