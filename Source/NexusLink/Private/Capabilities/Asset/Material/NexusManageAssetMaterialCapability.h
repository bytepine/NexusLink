// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/** manage_asset_material 的 Capability — 批量材质编辑（set_param/add_node/remove_node/set_node/recompile/connect/disconnect/disconnect_all）。*/
class FManageAssetMaterialCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
