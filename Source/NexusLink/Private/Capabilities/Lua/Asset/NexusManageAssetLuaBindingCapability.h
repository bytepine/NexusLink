// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_UNLUA
#include "NexusActionCapability.h"

/** manage_asset_lua_binding：绑定/解绑 Blueprint 的 UnLua 模块（禁止非 property 的 set_*）。 */
class FManageAssetLuaBindingCapability : public FNexusActionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override;
	virtual bool PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& OutError) const override;
	virtual void FinalizeTarget(void* Target) const override;
};
#endif
