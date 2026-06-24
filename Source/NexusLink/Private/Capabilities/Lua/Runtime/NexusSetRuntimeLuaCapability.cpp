// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusSetRuntimeLuaCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "NexusMcpTool.h"

void FSetRuntimeLuaCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("set_runtime_lua");
	Out.Description = TEXT("为 Lua 全局或嵌套字段赋值。点路径；string/number/bool/null。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("path"),  FNexusSchema::Str(TEXT("set 的点路径目标")))
		.Required(TEXT("value"), FNexusSchema::AnyObject(TEXT("值（string/number/boolean/null）")))
		.Build();
	Out.Tags = {FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("value"), TEXT("variable"), TEXT("field"), TEXT("path"), TEXT("assign") };
	Out.RelatedCapabilities = { TEXT("get_runtime_lua_value"), TEXT("eval_runtime_lua") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FSetRuntimeLuaCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;

	FString Path;
	if (!RequireString(Arguments, TEXT("path"), Path, R.Entries))
		return R;
	if (!Arguments->HasField(TEXT("value")))
	{
		EmitError(R.Entries, {{TEXT("path"), Path}}, TEXT("缺少 value"));
		return R;
	}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	const int32 StackTop = lua_gettop(L);

	// 将 JSON value 压入 Lua 栈
	TSharedPtr<FJsonValue> JsonVal = Arguments->TryGetField(TEXT("value"));
	if (!JsonVal.IsValid() || JsonVal->IsNull())
	{
		lua_pushnil(L);
	}
	else if (JsonVal->Type == EJson::Boolean)
	{
		lua_pushboolean(L, JsonVal->AsBool() ? 1 : 0);
	}
	else if (JsonVal->Type == EJson::Number)
	{
		lua_pushnumber(L, JsonVal->AsNumber());
	}
	else if (JsonVal->Type == EJson::String)
	{
		lua_pushstring(L, TCHAR_TO_UTF8(*JsonVal->AsString()));
	}
	else
	{
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("path"), Path}}, TEXT("value 仅支持 string/number/boolean/null"));
		return R;
	}

	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("."));

	if (Parts.Num() == 1)
	{
		lua_setglobal(L, TCHAR_TO_UTF8(*Parts[0]));
	}
	else
	{
		lua_getglobal(L, TCHAR_TO_UTF8(*Parts[0]));
		if (!lua_istable(L, -1))
		{
			lua_settop(L, StackTop);
			EmitError(R.Entries, {{TEXT("path"), Path}},
				FString::Printf(TEXT("'%s' 不是 table，无法设置子字段"), *Parts[0]));
			return R;
		}

		for (int32 i = 1; i < Parts.Num() - 1; ++i)
		{
			lua_getfield(L, -1, TCHAR_TO_UTF8(*Parts[i]));
			if (!lua_istable(L, -1))
			{
				lua_settop(L, StackTop);
				EmitError(R.Entries, {{TEXT("path"), Path}},
					FString::Printf(TEXT("Mid-path node '%s' is not a table"), *Parts[i]));
				return R;
			}
			lua_remove(L, -2);
		}

		// 栈顶是 parentTable，下面是先前压入的 value；复制 value 到栈顶后写回 parent
		const int32 ParentIdx = lua_gettop(L);
		lua_pushvalue(L, ParentIdx - 1);
		lua_setfield(L, ParentIdx, TCHAR_TO_UTF8(*Parts.Last()));
		lua_settop(L, StackTop);
	}

	Entry->SetBoolField(TEXT("success"), true);
	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FSetRuntimeLuaCapability)

#endif // WITH_UNLUA
