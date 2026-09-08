// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusExecPythonCapability.h"

#if WITH_NEXUS_PYTHON

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPythonRuntime.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Misc/Paths.h"

#include "IPythonScriptPlugin.h"

/** 单次调用回传的日志行上限，避免 Python 循环 print 打爆响应体。*/
static const int32 GNexusPythonMaxOutputLines = 200;

/** 单行字符上限；仅截行数挡不住 `print(huge_dict)` 这类单行超长输出。*/
static const int32 GNexusPythonMaxLineChars = 2000;

/** 超长单行截断并标注被丢弃的字符数。*/
static FString ClampPythonLine(const FString& Line)
{
	if (Line.Len() <= GNexusPythonMaxLineChars)
	{
		return Line;
	}
	return Line.Left(GNexusPythonMaxLineChars)
		+ FString::Printf(TEXT("... (%d chars truncated)"), Line.Len() - GNexusPythonMaxLineChars);
}

/** 合并 Python 捕获日志；Warning/Error 行加类型前缀，超出行数/行宽上限均截断。*/
static FString JoinPythonLogOutput(const TArray<FPythonLogOutputEntry>& LogOutput)
{
	FString Combined;
	const int32 Shown = FMath::Min(LogOutput.Num(), GNexusPythonMaxOutputLines);
	for (int32 i = 0; i < Shown; ++i)
	{
		if (!Combined.IsEmpty()) Combined += TEXT("\n");
		const FString Line = ClampPythonLine(LogOutput[i].Output);
		Combined += LogOutput[i].Type == EPythonLogOutputType::Info
			? Line
			: FString::Printf(TEXT("[%s] %s"), LexToString(LogOutput[i].Type), *Line);
	}
	if (LogOutput.Num() > Shown)
	{
		Combined += FString::Printf(TEXT("\n... %d more lines truncated"), LogOutput.Num() - Shown);
	}
	return Combined;
}

void FExecPythonCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("exec_python");
	Out.Description = TEXT("Run editor Python. exec/file/eval; stdout and traceback. Probe with get_python_api first.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("code"), FNexusSchema::Str(TEXT("Python source; required in exec/eval modes")))
		.Prop(TEXT("scriptPath"), FNexusSchema::Str(TEXT("Python file path relative to Content/Python/; required in file mode")))
		.Prop(TEXT("mode"), FNexusSchema::Enum(TEXT("Run source, run a .py file, or evaluate expression"),
			{ TEXT("exec"), TEXT("file"), TEXT("eval") }, TEXT("exec")))
		.Prop(TEXT("persistent"), FNexusSchema::Bool(TEXT("file mode: share console globals instead of isolated scope"), true, false))
		.Prop(TEXT("unattended"), FNexusSchema::Bool(TEXT("Suppress modal dialogs while running"), true, true))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("py"), TEXT("script"), TEXT("snippet"), TEXT("automation"), TEXT("scripting") };
	Out.RelatedCapabilities = { TEXT("get_python_api"), TEXT("exec_command"), TEXT("get_output_log") };
	Out.Prerequisites = { TEXT("python") };
}

FCapabilityResult FExecPythonCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	static const TArray<FString> ModeNames = { TEXT("exec"), TEXT("file"), TEXT("eval") };
	const int32 ModeIdx = A.EnumInt(TEXT("mode"), ModeNames, 0);
	const bool bFileMode = ModeIdx == 1;

	// file 模式走独立 scriptPath：UE 的 ExecuteFile 靠「首 token 是否 .py」自动分流，
	// 路径写错会被当字面代码执行并报出无关的 NameError，故在此显式校验
	FString Command;
	FString AbsPath;
	if (bFileMode)
	{
		const FString ScriptPath = A.Str(TEXT("scriptPath"));
		if (ScriptPath.IsEmpty())
		{
			return FCapabilityResult::MakeArgInvalid(TEXT("file mode requires scriptPath"));
		}
		if (!FPaths::IsRelative(ScriptPath) || ScriptPath.Contains(TEXT("..")))
		{
			return FCapabilityResult::MakeArgInvalid(
				TEXT("scriptPath must be relative to Content/Python/ and must not contain '..'"));
		}
		if (FPaths::GetExtension(ScriptPath) != TEXT("py"))
		{
			return FCapabilityResult::MakeArgInvalid(TEXT("scriptPath must end with .py"));
		}

		FString Root = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Python")));
		FPaths::NormalizeDirectoryName(Root);
		AbsPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Root, ScriptPath));
		FPaths::NormalizeFilename(AbsPath);
		FPaths::CollapseRelativeDirectories(AbsPath);

		if (!FPaths::IsUnderDirectory(AbsPath, Root))
		{
			return FCapabilityResult::MakeFatal(TEXT("scriptPath escapes Content/Python/"));
		}
		if (!FPaths::FileExists(AbsPath))
		{
			return FCapabilityResult::MakeFatal(FString::Printf(TEXT("File not found: %s"), *AbsPath));
		}
		// 加引号：UE 用 FParse::Token 取首 token，裸路径遇空格会被截断
		Command = FString::Printf(TEXT("\"%s\""), *AbsPath);
	}
	else
	{
		Command = A.Str(TEXT("code"));
		if (Command.IsEmpty())
		{
			return FCapabilityResult::MakeArgInvalid(TEXT("exec/eval mode requires code"));
		}
	}

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		IPythonScriptPlugin* Python = nullptr;
		if (!FNexusPythonRuntime::Acquire(Python, OutError))
		{
			return;
		}

		FPythonCommandEx Cmd;
		Cmd.Command = Command;
		// exec 与 file 共用 ExecuteFile：该模式既接受多语句字面脚本，也接受「文件路径 + 参数」
		Cmd.ExecutionMode = ModeIdx == 2
			? EPythonCommandExecutionMode::EvaluateStatement
			: EPythonCommandExecutionMode::ExecuteFile;
		// FileExecutionScope 只在跑 .py 文件时生效：Private 每次拷一份全局字典做隔离，
		// Public 复用 console 全局字典（字面代码走 RunString，本来就是 console 字典）
		if (bFileMode && A.Bool(TEXT("persistent"), false))
		{
			Cmd.FileExecutionScope = EPythonFileExecutionScope::Public;
		}
		if (A.Bool(TEXT("unattended"), true))
		{
			Cmd.Flags |= EPythonCommandFlags::Unattended;
		}

		const bool bExecuted = Python->ExecPythonCommandEx(Cmd);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("mode"), ModeNames[ModeIdx]);
		Entry->SetBoolField(TEXT("executed"), bExecuted);
		if (bFileMode)
		{
			Entry->SetStringField(TEXT("scriptPath"), AbsPath);
		}

		const FString Output = JoinPythonLogOutput(Cmd.LogOutput);
		if (!Output.IsEmpty())
		{
			Entry->SetStringField(TEXT("output"), Output);
		}

		if (bExecuted)
		{
			// CommandResult 仅 EvaluateStatement 有意义，其余模式为 "None"
			if (ModeIdx == 2 && !Cmd.CommandResult.IsEmpty())
			{
				Entry->SetStringField(TEXT("result"), Cmd.CommandResult);
			}
		}
		else
		{
			Entry->SetStringField(TEXT("error"),
				Cmd.CommandResult.IsEmpty() ? TEXT("Python execution failed") : Cmd.CommandResult);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FExecPythonCapability)

#endif // WITH_NEXUS_PYTHON
