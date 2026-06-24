// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_UNLUA

#include "NexusCapability.h"

/** manage_runtime_lua_hotreload — 触发 UnLua 2.X HotReload。 */
class FHotReloadRuntimeLuaCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_UNLUA
