// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusHotReloadRuntimeLuaCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "NexusMcpTool.h"

void FHotReloadRuntimeLuaCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("hotreload_runtime_lua");
	Out.Description = TEXT("热重载 UnLua 模块（2.x，无需重启 PIE）。UnLua 1.x 不执行热重载，返回 error。");
	Out.InputSchema = FNexusSchema::EmptyObject();
	Out.Tags = {FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("reload"), TEXT("refresh"), TEXT("module"), TEXT("unlua"), TEXT("update") };
	Out.RelatedCapabilities = { TEXT("dofile_runtime_lua"), TEXT("eval_runtime_lua") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FHotReloadRuntimeLuaCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FCapabilityResult R;
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

#if UNLUA_VERSION_MAJOR >= 2
	FNexusMcpToolResult Tmp;
	UnLua::FLuaEnv* LuaEnv = FNexusLuaUtils::GetLuaEnv(Tmp);
	if (!LuaEnv)
	{
		EmitError(R.Entries, {}, Tmp.ErrorText);
		return R;
	}

	lua_State* L = LuaEnv->GetMainState();
	const int32 MemBeforeKB = lua_gc(L, LUA_GCCOUNT, 0);
	LuaEnv->HotReload();
	const int32 MemAfterKB = lua_gc(L, LUA_GCCOUNT, 0);

	Entry->SetBoolField(TEXT("success"),        true);
	Entry->SetNumberField(TEXT("memoryBeforeKB"), MemBeforeKB);
	Entry->SetNumberField(TEXT("memoryAfterKB"),  MemAfterKB);
#else
	Entry->SetStringField(TEXT("error"),
		TEXT("hotreload is not supported on UnLua 1.X; restart the Lua environment manually or upgrade to UnLua 2.X"));
#endif

	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FHotReloadRuntimeLuaCapability)

#endif // WITH_UNLUA
