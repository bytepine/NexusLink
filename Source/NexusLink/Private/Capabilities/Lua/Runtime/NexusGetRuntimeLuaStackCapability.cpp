// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Runtime/NexusGetRuntimeLuaStackCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "NexusMcpTool.h"

// ── 调用栈遍历的局部 helpers ──

static void CollectLocals(lua_State* L, lua_Debug& Ar, TSharedRef<FJsonObject>& Frame)
{
	TArray<TSharedPtr<FJsonValue>> Locals;
	const char* LocalName;
	int32 LocalIdx = 1;
	while ((LocalName = lua_getlocal(L, &Ar, LocalIdx)) != nullptr)
	{
		if (LocalName[0] != '(')
		{
			TSharedRef<FJsonObject> Var = MakeShared<FJsonObject>();
			Var->SetStringField(TEXT("name"), UTF8_TO_TCHAR(LocalName));
		const int32 Type = lua_type(L, -1);
		Var->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, Type)));
		switch (Type)
		{
		case LUA_TSTRING:  Var->SetStringField(TEXT("value"), UTF8_TO_TCHAR(lua_tostring(L, -1))); break;
		case LUA_TNUMBER:  Var->SetNumberField(TEXT("value"), lua_tonumber(L, -1)); break;
		case LUA_TBOOLEAN: Var->SetBoolField(TEXT("value"), lua_toboolean(L, -1) != 0); break;
		case LUA_TNIL:     Var->SetStringField(TEXT("value"), TEXT("nil")); break;
		default:
			Var->SetStringField(TEXT("value"),
				FString::Printf(TEXT("<%s: %p>"), UTF8_TO_TCHAR(lua_typename(L, Type)), lua_topointer(L, -1)));
			break;
		}
		Locals.Add(MakeShared<FJsonValueObject>(Var));
		}
		lua_pop(L, 1);
		++LocalIdx;
	}
	if (Locals.Num() > 0) { Frame->SetArrayField(TEXT("locals"), Locals); }
}

static void CollectUpvalues(lua_State* L, lua_Debug& Ar, TSharedRef<FJsonObject>& Frame)
{
	if (!lua_getinfo(L, "f", &Ar)) { return; }

	TArray<TSharedPtr<FJsonValue>> Upvalues;
	int32 UpIdx = 1;
	const char* UpName;
	while ((UpName = lua_getupvalue(L, -1, UpIdx)) != nullptr)
	{
		TSharedRef<FJsonObject> Var = MakeShared<FJsonObject>();
		Var->SetStringField(TEXT("name"), UTF8_TO_TCHAR(UpName));
		const int32 Type = lua_type(L, -1);
		Var->SetStringField(TEXT("type"), UTF8_TO_TCHAR(lua_typename(L, Type)));
		switch (Type)
		{
		case LUA_TSTRING:  Var->SetStringField(TEXT("value"), UTF8_TO_TCHAR(lua_tostring(L, -1))); break;
		case LUA_TNUMBER:  Var->SetNumberField(TEXT("value"), lua_tonumber(L, -1)); break;
		case LUA_TBOOLEAN: Var->SetBoolField(TEXT("value"), lua_toboolean(L, -1) != 0); break;
		case LUA_TNIL:     Var->SetStringField(TEXT("value"), TEXT("nil")); break;
		default:
			Var->SetStringField(TEXT("value"),
				FString::Printf(TEXT("<%s: %p>"), UTF8_TO_TCHAR(lua_typename(L, Type)), lua_topointer(L, -1)));
			break;
		}
		lua_pop(L, 1);
		Upvalues.Add(MakeShared<FJsonValueObject>(Var));
		++UpIdx;
	}
	lua_pop(L, 1);

	if (Upvalues.Num() > 0) { Frame->SetArrayField(TEXT("upvalues"), Upvalues); }
}

// ── Capability 实现 ──

void FGetRuntimeLuaStackCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_stack");
	Out.Description = TEXT("转储 Lua 调用栈与局部/上值。detail=locals|upvalues|all。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedRef<FJsonObject> FrameIdxItem = MakeShared<FJsonObject>();
		FrameIdxItem->SetStringField(TEXT("type"), TEXT("number"));

		return FNexusSchema::Object()
		.Prop(TEXT("detail"),       FNexusSchema::Enum(TEXT("栈帧详情"),
		{ TEXT("summary"), TEXT("locals"), TEXT("upvalues"), TEXT("all") }, TEXT("summary")))
		.Prop(TEXT("frameIndex"),   FNexusSchema::Int(TEXT("要钻取的单个栈帧")))
		.Prop(TEXT("frameIndices"), FNexusSchema::ArrayOf(TEXT("要钻取的多个栈帧"), FrameIdxItem))
		.Prop(TEXT("sourceFilter"), FNexusSchema::Str(TEXT("栈帧源路径过滤")))
		.Prop(TEXT("maxDepth"),     FNexusSchema::Int(TEXT("最大栈帧数"), 50, 1, 500))
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("traceback"), TEXT("trace"), TEXT("frame"), TEXT("debug"), TEXT("callstack") };
	Out.RelatedCapabilities = { TEXT("eval_runtime_lua") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("pie") };
}

FCapabilityResult FGetRuntimeLuaStackCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FNexusMcpToolResult Tmp;
	lua_State* L = FNexusLuaUtils::GetMainLuaState(Tmp);
	if (!L)
		return FCapabilityResult::MakeFatal(Tmp.ErrorText);

	FString Detail = TEXT("summary");
	FString SourceFilter;
	int32 MaxDepth = 50;
	TSet<int32> DrillFrameSet;

	if (Arguments->HasField(TEXT("detail")))
		Detail = Arguments->GetStringField(TEXT("detail")).ToLower();
	if (Arguments->HasField(TEXT("sourceFilter")))
		SourceFilter = Arguments->GetStringField(TEXT("sourceFilter"));
	if (Arguments->HasField(TEXT("maxDepth")))
		MaxDepth = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("maxDepth"))), 1, 500);

	const TArray<TSharedPtr<FJsonValue>>* IndicesArr = nullptr;
	if (Arguments->TryGetArrayField(TEXT("frameIndices"), IndicesArr))
	{
		for (const TSharedPtr<FJsonValue>& Val : *IndicesArr)
			DrillFrameSet.Add(static_cast<int32>(Val->AsNumber()));
	}
	else if (Arguments->HasField(TEXT("frameIndex")))
	{
		DrillFrameSet.Add(static_cast<int32>(Arguments->GetNumberField(TEXT("frameIndex"))));
	}

	const bool bDrillMode    = DrillFrameSet.Num() > 0;
	const bool bWantLocals   = bDrillMode || Detail == TEXT("locals")   || Detail == TEXT("all");
	const bool bWantUpvalues = bDrillMode || Detail == TEXT("upvalues") || Detail == TEXT("all");

	TArray<TSharedPtr<FJsonValue>> Frames;
	lua_Debug Ar;
	int32 Level = 0, TotalDepth = 0;

	while (Level < MaxDepth && lua_getstack(L, Level, &Ar))
	{
		lua_getinfo(L, "nSltu", &Ar);
		++TotalDepth;

		FString Source = Ar.source ? FString(UTF8_TO_TCHAR(Ar.source)) : FString();

		if (!SourceFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Source, SourceFilter))
		{
			++Level;
			continue;
		}
		if (bDrillMode && !DrillFrameSet.Contains(Level))
		{
			++Level;
			continue;
		}

		TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
		Frame->SetNumberField(TEXT("level"), Level);
		if (!Source.IsEmpty())                    Frame->SetStringField(TEXT("source"),       Source);
		if (Ar.short_src[0] != '\0')              Frame->SetStringField(TEXT("shortSource"),  UTF8_TO_TCHAR(Ar.short_src));
		if (Ar.currentline > 0)                   Frame->SetNumberField(TEXT("currentLine"),  Ar.currentline);
		if (Ar.linedefined > 0)                   Frame->SetNumberField(TEXT("lineDefined"),  Ar.linedefined);
		if (Ar.lastlinedefined > 0)               Frame->SetNumberField(TEXT("lastLineDefined"), Ar.lastlinedefined);
		if (Ar.name)                              Frame->SetStringField(TEXT("name"),         UTF8_TO_TCHAR(Ar.name));
		if (Ar.namewhat && Ar.namewhat[0] != '\0') Frame->SetStringField(TEXT("nameWhat"),    UTF8_TO_TCHAR(Ar.namewhat));
		if (Ar.what)                              Frame->SetStringField(TEXT("what"),         UTF8_TO_TCHAR(Ar.what));

		if (bWantLocals)   CollectLocals(L, Ar, Frame);
		if (bWantUpvalues) CollectUpvalues(L, Ar, Frame);

		Frames.Add(MakeShared<FJsonValueObject>(Frame));
		++Level;
	}
	while (lua_getstack(L, Level, &Ar)) { ++TotalDepth; ++Level; }

	FCapabilityResult R;
	TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
	OutEntry->SetNumberField(TEXT("depth"),  TotalDepth);
	OutEntry->SetArrayField(TEXT("frames"), Frames);
	R.Entries.Add(MakeShared<FJsonValueObject>(OutEntry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeLuaStackCapability)

#endif // WITH_UNLUA

