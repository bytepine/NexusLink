// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGetRuntimeLuaLoadedCapability.h"
#include "Utils/NexusArgs.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "NexusMcpTool.h"

void FGetRuntimeLuaLoadedCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_loaded");
	Out.Description = TEXT("List package.loaded modules. Supports name filter.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("Key filter; /regex/, ^prefix, suffix$")))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("Max return count"), 100, 1, 500))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("modules"), TEXT("require"), TEXT("package"), TEXT("cache"), TEXT("imports") };
	Out.RelatedCapabilities = { TEXT("hotreload_runtime_lua"), TEXT("dofile_runtime_lua") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FGetRuntimeLuaLoadedCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;
	const int32 StackTop = lua_gettop(L);

	lua_getglobal(L, "package");
	if (!lua_istable(L, -1))
	{
		lua_settop(L, StackTop);
		return FCapabilityResult::MakeFatal(TEXT("package global table unavailable"));
	}
	lua_getfield(L, -1, "loaded");
	if (!lua_istable(L, -1))
	{
		lua_settop(L, StackTop);
		return FCapabilityResult::MakeFatal(TEXT("package.loaded unavailable"));
	}

	FString NameFilter;
	int32 Limit = 100;
	Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);
	if (Arguments->HasField(TEXT("limit")))
		Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("limit"))), 1, 500);

	const int32 LoadedIdx = lua_gettop(L);
	TArray<TSharedPtr<FJsonValue>> Modules;
	int32 TotalCount = 0;

	lua_pushnil(L);
	while (FNexusLuaUtils::SafeLuaNext(L, LoadedIdx) != 0)
	{
		if (lua_type(L, -2) == LUA_TSTRING)
		{
			const int ValType = lua_type(L, -1);
			if (ValType != LUA_TBOOLEAN)
			{
				FString ModName = UTF8_TO_TCHAR(lua_tostring(L, -2));
				if (NameFilter.IsEmpty() || FNexusStringMatchUtils::Matches(ModName, NameFilter))
				{
					++TotalCount;
					if (Modules.Num() < Limit)
					{
						TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
						Entry->SetStringField(TEXT("name"), ModName);
						Entry->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, ValType)));
						Modules.Add(MakeShared<FJsonValueObject>(Entry));
					}
				}
			}
		}
		lua_pop(L, 1);
	}

	lua_settop(L, StackTop);

	TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
	OutEntry->SetNumberField(TEXT("totalCount"), TotalCount);
	OutEntry->SetArrayField(TEXT("modules"), Modules);
	R.Entries.Add(MakeShared<FJsonValueObject>(OutEntry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeLuaLoadedCapability)

#endif // WITH_UNLUA

