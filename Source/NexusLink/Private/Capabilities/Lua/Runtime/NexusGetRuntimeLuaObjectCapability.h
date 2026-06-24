// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_UNLUA

#include "NexusCapability.h"

struct lua_State;

/** get_runtime_lua_object — 读取运行时 UObject 的实例/类 Lua 表。 */
class FGetRuntimeLuaObjectCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_UNLUA
