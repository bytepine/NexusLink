// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusDofileRuntimeLuaCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "Misc/Paths.h"
#include "NexusMcpTool.h"

void FDofileRuntimeLuaCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("dofile_runtime_lua");
	Out.Description = TEXT("从 Content/Script/ 加载执行 .lua。相对路径；需 UnLua+PIE。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("filePath"), FNexusSchema::Str(TEXT("Lua 文件路径（相对 Content/Script/）")))
		.Build();
	Out.Tags = {FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("require"), TEXT("script"), TEXT("file"), TEXT("load"), TEXT("execute") };
	Out.RelatedCapabilities = { TEXT("eval_runtime_lua"), TEXT("hotreload_runtime_lua") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FDofileRuntimeLuaCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FCapabilityResult R;

	FString FilePath;
	if (!RequireString(Arguments, TEXT("filePath"), FilePath, R.Entries))
		return R;

	FString AbsPath = FPaths::IsRelative(FilePath)
		? FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Script"), FilePath)
		: FilePath;
	FPaths::NormalizeFilename(AbsPath);

	if (!FPaths::FileExists(AbsPath))
		return FCapabilityResult::MakeFatal(FString::Printf(TEXT("文件不存在: %s"), *AbsPath));

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	const int32 StackTop = lua_gettop(L);

	if (luaL_loadfile(L, TCHAR_TO_UTF8(*AbsPath)) != 0)
	{
		FString ErrMsg = UTF8_TO_TCHAR(lua_tostring(L, -1));
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("filePath"), AbsPath}}, FString::Printf(TEXT("Lua 加载错误: %s"), *ErrMsg));
		return R;
	}

	if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0)
	{
		FString ErrMsg = UTF8_TO_TCHAR(lua_tostring(L, -1));
		lua_settop(L, StackTop);
		EmitError(R.Entries, {{TEXT("filePath"), AbsPath}}, FString::Printf(TEXT("Lua 执行错误: %s"), *ErrMsg));
		return R;
	}

	const int32 NumResults = lua_gettop(L) - StackTop;
	Entry->SetStringField(TEXT("filePath"), AbsPath);

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

REGISTER_MCP_CAPABILITY(FDofileRuntimeLuaCapability)

#endif // WITH_UNLUA
