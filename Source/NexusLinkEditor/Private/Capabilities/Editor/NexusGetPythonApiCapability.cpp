// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusGetPythonApiCapability.h"

#if WITH_NEXUS_PYTHON

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPythonRuntime.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

#include "IPythonScriptPlugin.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/** Python stdout 中探测结果的哨兵前缀。*/
static const TCHAR* GNexusPyApiSentinel = TEXT("NEXUS_PY_API:");

/** target 只允许 unreal 或 unreal.Xxx 点分标识符，杜绝脚本注入。*/
static bool IsSafePythonTarget(const FString& Target)
{
	if (!Target.StartsWith(TEXT("unreal")))
	{
		return false;
	}
	if (Target.Len() > 6 && Target[6] != TEXT('.'))
	{
		return false;
	}
	if (Target.EndsWith(TEXT(".")) || Target.Contains(TEXT("..")))
	{
		return false;
	}
	for (const TCHAR C : Target)
	{
		const bool bOk =
			(C >= TEXT('A') && C <= TEXT('Z')) ||
			(C >= TEXT('a') && C <= TEXT('z')) ||
			(C >= TEXT('0') && C <= TEXT('9')) ||
			C == TEXT('_') || C == TEXT('.');
		if (!bOk)
		{
			return false;
		}
	}
	return true;
}

/** query 只允许标识符字符（子串过滤，不含点）。*/
static bool IsSafePythonQuery(const FString& Query)
{
	for (const TCHAR C : Query)
	{
		const bool bOk =
			(C >= TEXT('A') && C <= TEXT('Z')) ||
			(C >= TEXT('a') && C <= TEXT('z')) ||
			(C >= TEXT('0') && C <= TEXT('9')) ||
			C == TEXT('_');
		if (!bOk)
		{
			return false;
		}
	}
	return true;
}

/** 从 Python 捕获日志中抽出哨兵行后的 JSON。*/
static FString ExtractSentinelJson(const TArray<FPythonLogOutputEntry>& LogOutput)
{
	const int32 PrefixLen = FCString::Strlen(GNexusPyApiSentinel);
	for (const FPythonLogOutputEntry& E : LogOutput)
	{
		const int32 Idx = E.Output.Find(GNexusPyApiSentinel);
		if (Idx != INDEX_NONE)
		{
			return E.Output.Mid(Idx + PrefixLen).TrimStartAndEnd();
		}
	}
	return FString();
}

void FNexusGetPythonApiCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetPythonApi");
	Out.Description = TEXT("Inspect unreal Python members. Live dir/signature for this engine bind. Probe before ExecPython.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Target"), FNexusSchema::Str(TEXT("Dotted unreal path to inspect"), TEXT("unreal")))
		.Prop(TEXT("Query"), FNexusSchema::Str(TEXT("Substring filter on member names")))
		.Prop(TEXT("bSearchDoc"), FNexusSchema::Bool(TEXT("Also match query against docstrings"), true, false))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Max members per page"), 30, 1, 100))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("inspect"), TEXT("signature"), TEXT("dir"), TEXT("Bind"), TEXT("module") };
	Out.RelatedCapabilities = { TEXT("ExecPython"), TEXT("GetEditorInfo") };
	Out.Prerequisites = { TEXT("python") };
}

FCapabilityResult FNexusGetPythonApiCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	FString Target = A.Str(TEXT("Target"), TEXT("unreal"));
	if (Target.IsEmpty())
	{
		Target = TEXT("unreal");
	}
	const FString Query = A.Str(TEXT("Query"));
	if (!IsSafePythonTarget(Target))
	{
		return FCapabilityResult::MakeArgInvalid(
			TEXT("target must be a dotted unreal.* path (letters, digits, underscore, dots)"));
	}
	if (!IsSafePythonQuery(Query))
	{
		return FCapabilityResult::MakeArgInvalid(
			TEXT("query may only contain letters, digits, and underscore"));
	}

	int32 Offset = 0;
	int32 Limit = 30;
	FNexusJsonUtils::ParseOffsetLimit(Arguments, Offset, Limit, 30, 100);
	const bool bSearchDoc = A.Bool(TEXT("bSearchDoc"), false);

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		IPythonScriptPlugin* Python = nullptr;
		if (!FNexusPythonRuntime::Acquire(Python, OutError))
		{
			return;
		}

		// target/query 已过白名单，作为字面量嵌入固定脚本，不接受用户代码
		const FString Script = FString::Printf(
			TEXT("import json,inspect,sys\n")
			TEXT("TARGET=\"%s\"\n")
			TEXT("QUERY=\"%s\"\n")
			TEXT("OFFSET=%d\n")
			TEXT("LIMIT=%d\n")
			TEXT("SEARCHDOC=%s\n")
			TEXT("PYVER=sys.version.split()[0]\n")
			TEXT("def _kind(o):\n")
			TEXT("  if inspect.isclass(o): return \"class\"\n")
			TEXT("  if inspect.ismodule(o): return \"module\"\n")
			TEXT("  if inspect.isroutine(o) or (callable(o) and not inspect.isclass(o)): return \"function\"\n")
			TEXT("  return \"other\"\n")
			TEXT("def _rawdoc(o):\n")
			TEXT("  try: return getattr(o,'__doc__',None) or ''\n")
			TEXT("  except Exception: return ''\n")
			TEXT("def _first(s):\n")
			TEXT("  if not s: return \"\"\n")
			TEXT("  return s.strip().splitlines()[0][:200]\n")
			TEXT("def _sig(o):\n")
			TEXT("  try:\n")
			TEXT("    return str(inspect.signature(o))\n")
			TEXT("  except Exception:\n")
			TEXT("    line=_first(_rawdoc(o))\n")
			TEXT("    return line if '(' in line else ''\n")
			TEXT("try:\n")
			TEXT("  parts=TARGET.split('.')\n")
			TEXT("  obj=__import__(parts[0])\n")
			TEXT("  for p in parts[1:]:\n")
			TEXT("    obj=getattr(obj,p)\n")
			TEXT("  names=[n for n in dir(obj) if not n.startswith('_')]\n")
			TEXT("  if QUERY:\n")
			TEXT("    q=QUERY.lower()\n")
			TEXT("    if SEARCHDOC:\n")
			TEXT("      sel=[]\n")
			TEXT("      for n in names:\n")
			TEXT("        if q in n.lower():\n")
			TEXT("          sel.append(n); continue\n")
			TEXT("        try: m=getattr(obj,n)\n")
			TEXT("        except Exception: continue\n")
			TEXT("        if q in _rawdoc(m).lower(): sel.append(n)\n")
			TEXT("      names=sel\n")
			TEXT("    else:\n")
			TEXT("      names=[n for n in names if q in n.lower()]\n")
			TEXT("  total=len(names)\n")
			TEXT("  entries=[]\n")
			TEXT("  for n in names[OFFSET:OFFSET+LIMIT]:\n")
			TEXT("    try: m=getattr(obj,n)\n")
			TEXT("    except Exception: continue\n")
			TEXT("    entries.append({'name':n,'Kind':_kind(m),'signature':_sig(m),'doc':_first(_rawdoc(m))})\n")
			TEXT("  payload={'Target':TARGET,'pythonVersion':PYVER,'totalMatched':total,'Offset':OFFSET,'Entries':entries}\n")
			TEXT("except Exception as e:\n")
			TEXT("  payload={'Target':TARGET,'pythonVersion':PYVER,'Error':type(e).__name__+': '+str(e)}\n")
			TEXT("print('NEXUS_PY_API:'+json.dumps(payload,ensure_ascii=True))\n"),
			*Target, *Query, Offset, Limit, bSearchDoc ? TEXT("True") : TEXT("False"));

		FPythonCommandEx Cmd;
		Cmd.Command = Script;
		Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Cmd.Flags |= EPythonCommandFlags::Unattended;

		const bool bExecuted = Python->ExecPythonCommandEx(Cmd);
		if (!bExecuted)
		{
			OutError = Cmd.CommandResult.IsEmpty()
				? TEXT("Python API probe failed")
				: Cmd.CommandResult;
			return;
		}

		const FString JsonLine = ExtractSentinelJson(Cmd.LogOutput);
		if (JsonLine.IsEmpty())
		{
			OutError = TEXT("Python API probe produced no result");
			return;
		}

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			OutError = TEXT("Failed to parse python API probe output");
			return;
		}

		// 成功与探测失败共用一条出口：「该 API 在本版本不存在」恰恰最需要版本信息，
		// 错误分支同样要带 EngineVersion / pythonVersion
		Parsed->SetStringField(TEXT("EngineVersion"), FString::Printf(TEXT("%d.%d.%d"),
			ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION));
		OutEntries.Add(MakeShared<FJsonValueObject>(Parsed));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetPythonApiCapability)

#endif // WITH_NEXUS_PYTHON
