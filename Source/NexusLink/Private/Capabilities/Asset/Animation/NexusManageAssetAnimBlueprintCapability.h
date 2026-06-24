// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * manage_anim_blueprint 的 Capability — 在 AnimBlueprint 的 AnimGraph 中
 * 管理状态机节点（StateMachine / State / Transition）。
 * 完整同步 EdGraph 与编译后的 BakedStateMachines。
 * 变量管理仍由 manage_blueprint_variable 负责。
 */
class FManageAssetAnimBlueprintCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
