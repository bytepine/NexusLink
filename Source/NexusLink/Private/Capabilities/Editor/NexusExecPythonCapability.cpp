// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusExecPythonCapability.h"

#if WITH_NEXUS_PYTHON

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

#include "IPythonScriptPlugin.h"

/** 单次调用回传的日志行上限，避免 Python 循环 print 打爆响应体。*/
static const int32 GNexusPythonMaxOutputLines = 200;

/** 合并 Python 捕获日志；Warning/Error 行加类型前缀，超出上限截断。*/
static FString JoinPythonLogOutput(const TArray<FPythonLogOutputEntry>& LogOutput)
{
	FString Combined;
	const int32 Shown = FMath::Min(LogOutput.Num(), GNexusPythonMaxOutputLines);
	for (int32 i = 0; i < Shown; ++i)
	{
		if (!Combined.IsEmpty()) Combined += TEXT("\n");
		Combined += LogOutput[i].Type == EPythonLogOutputType::Info
			? LogOutput[i].Output
			: FString::Printf(TEXT("[%s] %s"), LexToString(LogOutput[i].Type), *LogOutput[i].Output);
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
		.Prop(TEXT("code"), FNexusSchema::Str(TEXT("Python source, or file path in file mode")))
		.Prop(TEXT("mode"), FNexusSchema::Enum(TEXT("Run source, run a file, or evaluate expression"),
			{ TEXT("exec"), TEXT("file"), TEXT("eval") }, TEXT("exec")))
		.Prop(TEXT("unattended"), FNexusSchema::Bool(TEXT("Suppress modal dialogs while running"), true, true))
		.Required({ TEXT("code") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("py"), TEXT("script"), TEXT("snippet"), TEXT("automation"), TEXT("scripting") };
	Out.RelatedCapabilities = { TEXT("get_python_api"), TEXT("exec_command"), TEXT("get_output_log") };
}

FCapabilityResult FExecPythonCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python)
		{
			OutError = TEXT("Python plugin module not loaded");
			return;
		}
		if (!Python->IsPythonAvailable())
		{
			OutError = TEXT("Python support is not available in this editor build");
			return;
		}

		const FNexusArgs A(Arguments);
		static const TArray<FString> ModeNames = { TEXT("exec"), TEXT("file"), TEXT("eval") };
		const int32 ModeIdx = A.EnumInt(TEXT("mode"), ModeNames, 0);

		FPythonCommandEx Cmd;
		Cmd.Command = A.Str(TEXT("code"));
		// exec 与 file 共用 ExecuteFile：该模式既接受多语句字面脚本，也接受「文件路径 + 参数」
		Cmd.ExecutionMode = ModeIdx == 2
			? EPythonCommandExecutionMode::EvaluateStatement
			: EPythonCommandExecutionMode::ExecuteFile;
		if (A.Bool(TEXT("unattended"), true))
		{
			Cmd.Flags |= EPythonCommandFlags::Unattended;
		}

		const bool bExecuted = Python->ExecPythonCommandEx(Cmd);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("mode"), ModeNames[ModeIdx]);
		Entry->SetBoolField(TEXT("executed"), bExecuted);

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
