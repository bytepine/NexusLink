// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** create_asset_font：创建空白 UFont（Runtime 缓存类型；字形需 reimport/编辑器填充）。 */
class FCreateAssetFontCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
