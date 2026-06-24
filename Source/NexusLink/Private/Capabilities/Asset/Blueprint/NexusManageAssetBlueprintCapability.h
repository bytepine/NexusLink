// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

class UBlueprint;
class UEdGraph;

/** manage_asset_blueprint 的 Capability —— 增删/修改蓝图变量、图节点及连线（单次操作）。*/
class FManageAssetBlueprintCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
