// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** manage_asset_physics_asset — 编辑 PhysicsAsset：set_body_physics_type/add_sphere/add_capsule/add_box/remove_body_shapes/add_constraint/remove_constraint。 */
class FManageAssetPhysicsAssetCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
