// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGetRuntimeLuaMetatableCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "NexusMcpTool.h"

void FGetRuntimeLuaMetatableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_metatable");
	Out.Description = TEXT("沿 __index 链转储 OOP 类表。用于 UnLua 继承链调试。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("path"),     FNexusSchema::Str(TEXT("Lua 点分路径")))
		.Prop(TEXT("nameFilter"),   FNexusSchema::Str(TEXT("键名过滤；支持 /regex/、^前缀、后缀$")))
		.Prop(TEXT("limit"),        FNexusSchema::Int(TEXT("最大返回条数"), 100, 1, 500))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("mt"), TEXT("metamethod"), TEXT("class"), TEXT("oop"), TEXT("inheritance") };
	Out.RelatedCapabilities = { TEXT("get_runtime_lua_object"), TEXT("get_runtime_lua_env") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FGetRuntimeLuaMetatableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;

	FString Path;
	if (!RequireString(Arguments, TEXT("path"), Path, R.Entries))
		return R;

	const int32 StackTop = lua_gettop(L);

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

	if (!FNexusLuaUtils::ResolveLuaPath(L, Path))
	{
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("path"), Path}}, FString::Printf(TEXT("路径 '%s' 未找到"), *Path));
		return R;
	}

	Entry->SetStringField(TEXT("valueType"), UTF8_TO_TCHAR(lua_typename(L, lua_type(L, -1))));

	if (!lua_getmetatable(L, -1))
	{
		lua_settop(L, StackTop);
		Entry->SetBoolField(TEXT("hasMetatable"), false);
		R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		return R;
	}

	Entry->SetBoolField(TEXT("hasMetatable"), true);

	FString NameFilter;
	int32 Limit = 100;
	Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);
	if (Arguments->HasField(TEXT("limit")))
		Limit = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);

	const int32 MetaIdx = lua_gettop(L);
	TArray<TSharedPtr<FJsonValue>> Keys;
	int32 TotalCount = 0;

	lua_pushnil(L);
	while (FNexusLuaUtils::SafeLuaNext(L, MetaIdx) != 0)
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
			TSharedRef<FJsonObject> KEntry = MakeShared<FJsonObject>();
			KEntry->SetStringField(TEXT("key"),  Key);
			KEntry->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, lua_type(L, -1))));
			Keys.Add(MakeShared<FJsonValueObject>(KEntry));
		}
		lua_pop(L, 1);
	}

	Entry->SetNumberField(TEXT("totalCount"), TotalCount);
	Entry->SetArrayField(TEXT("keys"), Keys);

	// 检查 __index 链（UnLua 绑定常用多层 __index 继承）
	lua_getfield(L, MetaIdx, "__index");
	const bool bHasParent = lua_istable(L, -1) && lua_getmetatable(L, -1);
	if (bHasParent) lua_pop(L, 1);
	lua_pop(L, 1);
	Entry->SetBoolField(TEXT("hasParentMetatable"), bHasParent);

	lua_settop(L, StackTop);
	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeLuaMetatableCapability)

#endif // WITH_UNLUA

