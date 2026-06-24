// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusEvalRuntimeLuaCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "NexusMcpTool.h"

void FEvalRuntimeLuaCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("eval_runtime_lua");
	Out.Description = TEXT("在 PIE/Game 执行 Lua 片段，返回压栈值。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("code"), FNexusSchema::Str(TEXT("Lua 表达式或代码块")))
		.Build();
	Out.Tags = {FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("script"), TEXT("code"), TEXT("expression"), TEXT("repl"), TEXT("snippet") };
	Out.RelatedCapabilities = { TEXT("set_runtime_lua"), TEXT("get_runtime_lua_value") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FEvalRuntimeLuaCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;

	FString Code;
	if (!RequireString(Arguments, TEXT("code"), Code, R.Entries))
		return R;

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	const int32 StackTop = lua_gettop(L);

	// Best-effort：预注入 UE 全局，允许 AI 在裸 Lua 全局下直接写 UE.* 一行诊断。
	static const char* kBootstrap =
		"if rawget(_G, 'UE') == nil then\n"
		"  local ok, M = pcall(require, 'UnLua')\n"
		"  if ok and type(M) == 'table' and M.UE then _G.UE = M.UE end\n"
		"end";
	luaL_dostring(L, kBootstrap);
	lua_settop(L, StackTop);

	// 先尝试 "return <code>" 以获取返回值；失败则直接执行原始代码
	FString ExprCode = FString::Printf(TEXT("return %s"), *Code);
	int32 LoadResult = luaL_loadstring(L, TCHAR_TO_UTF8(*ExprCode));
	if (LoadResult != 0)
	{
		lua_pop(L, 1);
		LoadResult = luaL_loadstring(L, TCHAR_TO_UTF8(*Code));
		if (LoadResult != 0)
		{
			FString ErrMsg = UTF8_TO_TCHAR(lua_tostring(L, -1));
			lua_settop(L, StackTop);
			EmitError(R.Entries, {}, FString::Printf(TEXT("Lua 解析错误: %s"), *ErrMsg));
			return R;
		}
	}

	if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0)
	{
		FString ErrMsg = UTF8_TO_TCHAR(lua_tostring(L, -1));
		lua_settop(L, StackTop);
		EmitError(R.Entries, {}, FString::Printf(TEXT("Lua 求值错误: %s"), *ErrMsg));
		return R;
	}

	const int32 NumResults = lua_gettop(L) - StackTop;

	if (NumResults == 1)
	{
		Entry->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, lua_type(L, -1))));
		TSharedPtr<FJsonValue> Val = FNexusLuaUtils::LuaValueToJson(L, -1, 0);
		Entry->SetField(TEXT("value"), Val.IsValid() ? Val : MakeShared<FJsonValueNull>());
	}
	else if (NumResults > 1)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (int32 i = StackTop + 1; i <= lua_gettop(L); ++i)
		{
			TSharedPtr<FJsonValue> Val = FNexusLuaUtils::LuaValueToJson(L, i, 0);
			Values.Add(Val.IsValid() ? Val : MakeShared<FJsonValueNull>());
		}
		Entry->SetArrayField(TEXT("values"), Values);
	}

	lua_settop(L, StackTop);
	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FEvalRuntimeLuaCapability)

#endif // WITH_UNLUA
