// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGetRuntimeLuaValueCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "NexusMcpTool.h"

void FGetRuntimeLuaValueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_value");
	Out.Description = TEXT("Read Lua global or nested field by dot path. Returns type and value.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("luaPath"), FNexusSchema::Str(TEXT("Lua dot-separated path")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("variable"), TEXT("field"), TEXT("path"), TEXT("global"), TEXT("dot") };
	Out.RelatedCapabilities = { TEXT("set_runtime_lua"), TEXT("get_runtime_lua_env") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
	Out.WhenToUse = TEXT("Read single key here; browse keys with env");
}

FCapabilityResult FGetRuntimeLuaValueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;

	FString Path;
	if (!RequireString(Arguments, TEXT("luaPath"), Path, R.Entries))
		return R;

	const int32 StackTop = lua_gettop(L);
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("luaPath"), Path);

	if (!FNexusLuaUtils::ResolveLuaPath(L, Path))
	{
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("luaPath"), Path}},
			FString::Printf(TEXT("Path '%s' not found (nil or non-table intermediate)"), *Path));
		return R;
	}

	Entry->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, lua_type(L, -1))));
	TSharedPtr<FJsonValue> Val = FNexusLuaUtils::LuaValueToJson(L, -1, 0);
	Entry->SetField(TEXT("value"), Val.IsValid() ? Val : MakeShared<FJsonValueNull>());

	lua_settop(L, StackTop);
	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeLuaValueCapability)

#endif // WITH_UNLUA
