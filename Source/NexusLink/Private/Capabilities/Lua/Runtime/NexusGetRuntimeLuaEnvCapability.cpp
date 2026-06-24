// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGetRuntimeLuaEnvCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "Utils/NexusStringMatchUtils.h"

void FGetRuntimeLuaEnvCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_env");
	Out.Description = TEXT("枚举 _G 或嵌套表键。支持 nameFilter+limit。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("path"),       FNexusSchema::Str(TEXT("点分表路径；省略则为 _G")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("键名过滤；支持 /regex/、^前缀、后缀$")))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("最大返回条数"), 100, 1, 500))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("environment"), TEXT("global"), TEXT("table"), TEXT("keys"), TEXT("scope") };
	Out.RelatedCapabilities = { TEXT("get_runtime_lua_value"), TEXT("get_runtime_lua_object") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
	Out.WhenToUse = TEXT("浏览所有键（env）或读单键（value）");
}

FCapabilityResult FGetRuntimeLuaEnvCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;
	const int32 StackTop = lua_gettop(L);

	FString TablePath;
	Arguments->TryGetStringField(TEXT("path"), TablePath);

	if (TablePath.IsEmpty())
	{
		lua_pushglobaltable(L);
	}
	else if (!FNexusLuaUtils::ResolveLuaPath(L, TablePath))
	{
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("path"), TablePath}}, FString::Printf(TEXT("路径 '%s' 未找到"), *TablePath));
		return R;
	}

	if (!lua_istable(L, -1))
	{
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("path"), TablePath}}, FString::Printf(TEXT("'%s' 不是 table"), *TablePath));
		return R;
	}

	FString NameFilter;
	int32 Limit = 100;
	Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);
	if (Arguments->HasField(TEXT("limit")))
		Limit = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);

	const int32 AbsIdx = lua_gettop(L);
	TArray<TSharedPtr<FJsonValue>> Keys;
	int32 TotalCount = 0;

	lua_pushnil(L);
	while (FNexusLuaUtils::SafeLuaNext(L, AbsIdx) != 0)
	{
		FString Key;
		const int KT = lua_type(L, -2);
		if (KT == LUA_TSTRING)      Key = UTF8_TO_TCHAR(lua_tostring(L, -2));
		else if (KT == LUA_TNUMBER) Key = FString::Printf(TEXT("[%d]"), static_cast<int>(lua_tonumber(L, -2)));
		else                        Key = FString::Printf(TEXT("<%s>"), UTF8_TO_TCHAR(lua_typename(L, KT)));

		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Key, NameFilter))
		{
			lua_pop(L, 1);
			continue;
		}

		++TotalCount;
		if (Keys.Num() < Limit)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("key"),  Key);
			Entry->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, lua_type(L, -1))));
			Keys.Add(MakeShared<FJsonValueObject>(Entry));
		}
		lua_pop(L, 1);
	}

	lua_settop(L, StackTop);

	TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
	OutEntry->SetNumberField(TEXT("totalCount"), TotalCount);
	OutEntry->SetArrayField(TEXT("keys"), Keys);
	R.Entries.Add(MakeShared<FJsonValueObject>(OutEntry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeLuaEnvCapability)

#endif // WITH_UNLUA

