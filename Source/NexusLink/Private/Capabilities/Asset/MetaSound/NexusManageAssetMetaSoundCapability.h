// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

#if WITH_METASOUND

/** manage_asset_meta_sound — 修改 MetaSound Source：add_input/remove_input/add_output/remove_output。 */
class FManageAssetMetaSoundCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_METASOUND
