// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_UNLUA
#include "NexusCapability.h"

/** manage_asset_lua_binding：绑定/解绑 Blueprint 的 UnLua 模块（禁止非 property 的 set_*）。 */
class FManageAssetLuaBindingCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
