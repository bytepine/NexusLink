// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGcRuntimeLuaCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "NexusMcpTool.h"

void FGcRuntimeLuaCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("gc_runtime_lua");
	Out.Description = TEXT("控制 PIE 中 Lua GC。mode=collect|stop|restart|count。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("mode"), FNexusSchema::Enum(TEXT("GC 模式"),
			{ TEXT("collect"), TEXT("stop"), TEXT("restart"), TEXT("count") }, TEXT("collect")))
		.Build();
	Out.Tags = {FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("collect"), TEXT("garbage"), TEXT("memory"), TEXT("cleanup"), TEXT("reclaim") };
	Out.RelatedCapabilities = { TEXT("get_runtime_lua_memory") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FGcRuntimeLuaCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FString Mode = TEXT("collect");
	if (Arguments->HasField(TEXT("mode")))
		Mode = Arguments->GetStringField(TEXT("mode")).ToLower();

	FCapabilityResult R;
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

	const int32 MemBeforeKB = lua_gc(L, LUA_GCCOUNT,  0);
	const int32 MemBeforeB  = lua_gc(L, LUA_GCCOUNTB, 0);
	const int32 BeforeBytes = MemBeforeKB * 1024 + MemBeforeB;

	if (Mode == TEXT("collect"))
	{
		lua_gc(L, LUA_GCCOLLECT, 0);
		const int32 MemAfterKB = lua_gc(L, LUA_GCCOUNT,  0);
		const int32 MemAfterB  = lua_gc(L, LUA_GCCOUNTB, 0);
		Entry->SetStringField(TEXT("mode"),          TEXT("collect"));
		Entry->SetNumberField(TEXT("memoryBeforeKB"), MemBeforeKB);
		Entry->SetNumberField(TEXT("memoryAfterKB"),  MemAfterKB);
		Entry->SetNumberField(TEXT("freedBytes"),     BeforeBytes - (MemAfterKB * 1024 + MemAfterB));
	}
	else if (Mode == TEXT("stop"))
	{
		lua_gc(L, LUA_GCSTOP, 0);
		Entry->SetStringField(TEXT("mode"),      TEXT("stop"));
		Entry->SetNumberField(TEXT("memoryKB"),  MemBeforeKB);
	}
	else if (Mode == TEXT("restart"))
	{
		lua_gc(L, LUA_GCRESTART, 0);
		Entry->SetStringField(TEXT("mode"),      TEXT("restart"));
		Entry->SetNumberField(TEXT("memoryKB"),  MemBeforeKB);
	}
	else if (Mode == TEXT("count"))
	{
		Entry->SetStringField(TEXT("mode"),        TEXT("count"));
		Entry->SetNumberField(TEXT("memoryKB"),    MemBeforeKB);
		Entry->SetNumberField(TEXT("memoryBytes"), BeforeBytes);
	}
	else
	{
		EmitError(R.Entries, {}, FString::Printf(
			TEXT("不支持的 mode: %s（期望 collect/stop/restart/count）"), *Mode));
		return R;
	}

	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGcRuntimeLuaCapability)

#endif // WITH_UNLUA
