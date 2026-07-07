// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** manage_asset_level_sequence — 编辑 LevelSequence：set_display_rate/set_range/add_spawnable/remove_binding/add_master_track/remove_master_track。 */
class FManageAssetLevelSequenceCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
