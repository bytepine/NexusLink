// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UWidgetBlueprint;

/** manage_asset_widget 的 Capability — 批量增删 WidgetBlueprint 控件（widgets[]）。*/
class FManageAssetUserWidgetCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
