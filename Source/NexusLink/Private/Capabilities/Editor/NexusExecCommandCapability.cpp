// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusExecCommandCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor/NexusLogCapture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#if WITH_EDITOR
#include "Editor.h"
#include "NexusMcpTool.h"
#endif

/** 临时 OutputDevice，拦截 Exec 输出到字符串缓冲区。*/
class FNexusCapExecOutputDevice : public FOutputDevice
{
public:
	FString Buffer;
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
	{
		if (!Buffer.IsEmpty()) Buffer += TEXT("\n");
		Buffer += V;
	}
};

/** exec_command 控制台输出在 get_output_log 中的分类名。*/
static const TCHAR* GExecLogCategory = TEXT("LogConsole");

/** 将 exec 捕获到的控制台输出镜像到日志缓冲区，供 get_output_log 查询。*/
static void MirrorExecOutputToLogCapture(const FString& Command, const FString& Output)
{
	if (Output.IsEmpty()) return;

	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty()) continue;
		FNexusLogCapture::Get().AppendEntry(
			GExecLogCategory,
			ELogVerbosity::Display,
			FString::Printf(TEXT("[exec: %s] %s"), *Command, *Line));
	}
}

/** 合并 Exec OutputDevice 与 exec 期间 GLog 增量（如 help stat 走 LogEngine）。*/
static FString MergeExecOutput(const FString& DeviceOutput, int32 LogMark)
{
	TSet<FString> SeenLines;
	FString Combined;

	auto AppendLine = [&](const FString& Line)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || SeenLines.Contains(Trimmed)) return;
		SeenLines.Add(Trimmed);
		if (!Combined.IsEmpty()) Combined += TEXT("\n");
		Combined += Trimmed;
	};

	TArray<FString> DeviceLines;
	DeviceOutput.ParseIntoArrayLines(DeviceLines, false);
	for (const FString& Line : DeviceLines)
	{
		AppendLine(Line);
	}

	for (const FNexusLogEntry& E : FNexusLogCapture::Get().CollectSince(LogMark))
	{
		// 跳过 NexusLink MCP 自身日志，避免污染 exec 输出
		if (E.Category.StartsWith(TEXT("LogNexus"))) continue;
		AppendLine(E.Message);
	}

	return Combined;
}

void FExecCommandCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("exec_command");
	Out.Description = TEXT("执行 UE 控制台命令并捕获 output（含 LogEngine 等 GLog 输出）。镜像到 LogConsole。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("command"), FNexusSchema::Str(TEXT("要执行的控制台命令")))
		.Prop(TEXT("silent"),  FNexusSchema::Bool(TEXT("跳过捕获输出"), false))
		.Required({ TEXT("command") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("console"), TEXT("cmd"), TEXT("cvar"), TEXT("stat"), TEXT("run") };
	Out.RelatedCapabilities = { TEXT("get_output_log") };
}

FCapabilityResult FExecCommandCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString Command;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
		{ OutError = TEXT("缺少 command"); return; }

		bool bSilent = false;
		Arguments->TryGetBoolField(TEXT("silent"), bSilent);

		if (!GEngine) { OutError = TEXT("GEngine 不可用"); return; }

		UWorld* World = nullptr;
	#if WITH_EDITOR
		if (GEditor)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::PIE && Ctx.World()) { World = Ctx.World(); break; }
			}
		}
	#endif
		if (!World)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World()) { World = Ctx.World(); break; }
			}
		}

		const int32 LogMark = bSilent ? 0 : FNexusLogCapture::Get().GetTotalWritten();

		bool bExecResult = false;
		FString DeviceOutput;

		if (bSilent)
		{
			bExecResult = GEngine->Exec(World, *Command);
		}
		else
		{
			FNexusCapExecOutputDevice OutputDevice;
			bExecResult = GEngine->Exec(World, *Command, OutputDevice);
			DeviceOutput = OutputDevice.Buffer;
		}

		if (!bExecResult && World)
		{
			APlayerController* PC = World->GetFirstPlayerController();
			if (PC)
			{
				const FString PCOutput = PC->ConsoleCommand(Command, !bSilent);
				bExecResult = true;
				if (!bSilent && DeviceOutput.IsEmpty() && !PCOutput.IsEmpty())
					DeviceOutput = PCOutput;
			}
		}

		const FString Output = bSilent ? FString() : MergeExecOutput(DeviceOutput, LogMark);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("command"),  Command);
		OutEntry->SetBoolField(TEXT("executed"),   bExecResult);
		if (!bSilent && !Output.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("output"), Output);
			MirrorExecOutputToLogCapture(Command, Output);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FExecCommandCapability)

#endif // WITH_EDITOR
