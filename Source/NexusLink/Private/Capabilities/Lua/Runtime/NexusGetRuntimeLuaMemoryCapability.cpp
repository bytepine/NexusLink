// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGetRuntimeLuaMemoryCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "NexusMcpTool.h"

void FGetRuntimeLuaMemoryCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_memory");
	Out.Description = TEXT("报告 Lua VM 堆用量（KB/字节）。配合 gc_runtime_lua 诊断。");
	Out.InputSchema = FNexusSchema::EmptyObject();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("heap"), TEXT("usage"), TEXT("stats"), TEXT("ram"), TEXT("kb") };
	Out.RelatedCapabilities = { TEXT("gc_runtime_lua") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
	Out.WhenToUse = TEXT("gc collect 前后检查堆大小");
}

FCapabilityResult FGetRuntimeLuaMemoryCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	const int KBCount      = lua_gc(L, LUA_GCCOUNT,  0);
	const int ByteRemainder = lua_gc(L, LUA_GCCOUNTB, 0);

	FCapabilityResult R;
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetNumberField(TEXT("memoryKB"),    KBCount);
	Entry->SetNumberField(TEXT("memoryBytes"), KBCount * 1024 + ByteRemainder);
	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeLuaMemoryCapability)

#endif // WITH_UNLUA
