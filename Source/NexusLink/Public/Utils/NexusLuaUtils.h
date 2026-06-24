// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Domain（依赖 WITH_UNLUA）
#include "CoreMinimal.h"
#include "NexusMcpTool.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Utils/NexusStringMatchUtils.h"

#if WITH_UNLUA
#include "UnLua.h"

#if UNLUA_VERSION_MAJOR >= 2
#include "LuaEnv.h"
#else
#include "LuaContext.h"
#endif

/** UnLua 共用静态工具（环境、栈、路径解析、table 枚举等）。 */
class NEXUSLINK_API FNexusLuaUtils
{
public:
#if UNLUA_VERSION_MAJOR >= 2
	/** 获取第一个可用的 FLuaEnv（UnLua 2.X），失败时填充错误信息并返回 nullptr。 */
	static UnLua::FLuaEnv* GetLuaEnv(FNexusMcpToolResult& R)
	{
		const TMap<lua_State*, UnLua::FLuaEnv*>& AllEnvs = UnLua::FLuaEnv::GetAll();
		for (auto& Pair : AllEnvs)
		{
			if (Pair.Value) return Pair.Value;
		}
		R.bIsError = true;
		R.ErrorText = TEXT("UnLua environment not ready; ensure UnLua is initialized with an active Game World");
		return nullptr;
	}
#endif

	/** 获取第一个可用的 lua_State，失败时填充错误信息并返回 nullptr。 */
	static lua_State* GetMainLuaState(FNexusMcpToolResult& R)
	{
#if UNLUA_VERSION_MAJOR >= 2
		UnLua::FLuaEnv* LuaEnv = GetLuaEnv(R);
		if (!LuaEnv) return nullptr;
		lua_State* L = LuaEnv->GetMainState();
#else
		FLuaContext* Ctx = FLuaContext::GetCtx();
		if (!Ctx || !Ctx->IsEnable())
		{
			R.bIsError = true;
			R.ErrorText = TEXT("UnLua environment not ready; ensure UnLua is initialized with an active Game World");
			return nullptr;
		}
		lua_State* L = static_cast<lua_State*>(*Ctx);
#endif
		if (!L)
		{
			R.bIsError = true;
			R.ErrorText = TEXT("lua_State is null");
			return nullptr;
		}
		return L;
	}

	/** 通过反射调用 IUnLuaInterface::GetModuleName，避免硬依赖 UnLua 接口头。 */
	static FString GetUnLuaModuleName(UClass* InClass)
	{
		if (!InClass) return FString();
		UObject* CDO = InClass->GetDefaultObject(false);
		if (!CDO) return FString();
		UFunction* Fn = CDO->FindFunction(TEXT("GetModuleName"));
		if (!Fn) return FString();
		struct { FString ReturnValue; } Params;
		CDO->ProcessEvent(Fn, &Params);
		return Params.ReturnValue;
	}

	/** SEH 保护下调用 lua_next，防止遍历损坏 table 时崩溃。 */
	static int SafeLuaNext(lua_State* L, int TableIndex)
	{
#if PLATFORM_WINDOWS
		__try { return lua_next(L, TableIndex); }
		__except (1) { return 0; }
#else
		return lua_next(L, TableIndex);
#endif
	}

	/** Lua 栈值 → FJsonValue，table 递归深度/条目数有限制。 */
	static TSharedPtr<FJsonValue> LuaValueToJson(lua_State* L, int Index, int Depth = 0)
	{
		constexpr int MaxDepth = 3;
		constexpr int MaxEntries = 50;

		if (Depth > MaxDepth)      return MakeShared<FJsonValueString>(TEXT("<max depth>"));
		if (!lua_checkstack(L, 4)) return MakeShared<FJsonValueString>(TEXT("<stack overflow>"));

		int Type = lua_type(L, Index);
		switch (Type)
		{
		case LUA_TSTRING:
			return MakeShared<FJsonValueString>(FString(UTF8_TO_TCHAR(lua_tostring(L, Index))));
		case LUA_TNUMBER:
			return MakeShared<FJsonValueNumber>(lua_tonumber(L, Index));
		case LUA_TBOOLEAN:
			return MakeShared<FJsonValueBoolean>(lua_toboolean(L, Index) != 0);
		case LUA_TNIL:
			return MakeShared<FJsonValueNull>();
		case LUA_TTABLE:
		{
			if (Depth >= 2) return MakeShared<FJsonValueString>(TEXT("<table>"));
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			int AbsIdx = (Index > 0) ? Index : (lua_gettop(L) + Index + 1);
			lua_pushnil(L);
			int Count = 0;
			while (SafeLuaNext(L, AbsIdx) != 0 && Count < MaxEntries)
			{
				int KT = lua_type(L, -2);
				int VT = lua_type(L, -1);
				if (KT == LUA_TNONE || VT == LUA_TNONE) { lua_pop(L, 2); break; }

				FString Key;
				if (KT == LUA_TSTRING)      Key = UTF8_TO_TCHAR(lua_tostring(L, -2));
				else if (KT == LUA_TNUMBER) Key = FString::Printf(TEXT("[%d]"), static_cast<int>(lua_tonumber(L, -2)));
				else                         Key = FString::Printf(TEXT("<%s>"), UTF8_TO_TCHAR(lua_typename(L, KT)));

				TSharedPtr<FJsonValue> Val = LuaValueToJson(L, -1, Depth + 1);
				Obj->SetField(Key, Val.IsValid() ? Val : MakeShared<FJsonValueNull>());
				lua_pop(L, 1);
				++Count;
			}
			if (Count >= MaxEntries)
			{
				Obj->SetStringField(TEXT("..."), TEXT("<truncated>"));
				lua_pop(L, 1);
			}
			return MakeShared<FJsonValueObject>(Obj);
		}
		case LUA_TFUNCTION:
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("<function: %p>"), lua_topointer(L, Index)));
		case LUA_TUSERDATA:
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("<userdata: %p>"), lua_topointer(L, Index)));
		case LUA_TLIGHTUSERDATA:
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("<lightuserdata: %p>"), lua_topointer(L, Index)));
		default:
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("<%s>"), UTF8_TO_TCHAR(lua_typename(L, Type))));
		}
	}

	/** 枚举 Lua table 的 keys，支持过滤与分页。 */
	static void CollectTableKeys(lua_State* L, int TableIdx, const FString& NameFilter,
		int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutKeys, int32& OutTotal)
	{
		int AbsIdx = (TableIdx > 0) ? TableIdx : (lua_gettop(L) + TableIdx + 1);
		lua_pushnil(L);
		while (SafeLuaNext(L, AbsIdx) != 0)
		{
			FString Key;
			int KT = lua_type(L, -2);
			if (KT == LUA_TSTRING)      Key = UTF8_TO_TCHAR(lua_tostring(L, -2));
			else if (KT == LUA_TNUMBER) Key = FString::Printf(TEXT("[%d]"), static_cast<int>(lua_tonumber(L, -2)));
			else                         Key = FString::Printf(TEXT("<%s>"), UTF8_TO_TCHAR(lua_typename(L, KT)));

			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Key, NameFilter))
			{
				lua_pop(L, 1);
				continue;
			}

			++OutTotal;
			if (OutKeys.Num() < Limit)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("key"), Key);
				int VT = lua_type(L, -1);
				Entry->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, VT)));
				switch (VT)
				{
				case LUA_TSTRING:  Entry->SetStringField(TEXT("value"), UTF8_TO_TCHAR(lua_tostring(L, -1))); break;
				case LUA_TNUMBER:  Entry->SetNumberField(TEXT("value"), lua_tonumber(L, -1)); break;
				case LUA_TBOOLEAN: Entry->SetBoolField(TEXT("value"), lua_toboolean(L, -1) != 0); break;
				default: break;
				}
				OutKeys.Add(MakeShared<FJsonValueObject>(Entry));
			}
			lua_pop(L, 1);
		}
	}

	/** 将 package.loaded[ModuleName] 压栈（table 类型），失败时恢复栈并返回 false。 */
	static bool PushLoadedModule(lua_State* L, const FString& ModuleName)
	{
		lua_getglobal(L, "package");
		if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
		lua_getfield(L, -1, "loaded");
		if (!lua_istable(L, -1)) { lua_pop(L, 2); return false; }
		lua_getfield(L, -1, TCHAR_TO_UTF8(*ModuleName));
		if (!lua_istable(L, -1)) { lua_pop(L, 3); return false; }
		lua_remove(L, -2);
		lua_remove(L, -2);
		return true;
	}

	/**
	 * 按点分路径解析 Lua 值并压栈。
	 * 成功返回 true（栈顶为目标值），失败返回 false 且栈已恢复。
	 */
	static bool ResolveLuaPath(lua_State* L, const FString& Path)
	{
		int StackTop = lua_gettop(L);

		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0)
		{
			lua_pushglobaltable(L);
			return true;
		}

		lua_getglobal(L, TCHAR_TO_UTF8(*Parts[0]));
		if (lua_isnil(L, -1))
		{
			lua_settop(L, StackTop);
			return false;
		}

		for (int32 i = 1; i < Parts.Num(); ++i)
		{
			if (!lua_istable(L, -1))
			{
				lua_settop(L, StackTop);
				return false;
			}
			lua_getfield(L, -1, TCHAR_TO_UTF8(*Parts[i]));
			lua_remove(L, -2);
			if (lua_isnil(L, -1))
			{
				lua_settop(L, StackTop);
				return false;
			}
		}
		return true;
	}
};

#endif // WITH_UNLUA

