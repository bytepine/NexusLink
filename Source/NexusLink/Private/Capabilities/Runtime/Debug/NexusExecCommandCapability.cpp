// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Debug/NexusExecCommandCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Log/NexusLogCapture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Player.h"
#include "GameFramework/PlayerController.h"
#include "Misc/OutputDeviceNull.h"
#include "NexusMcpTool.h"

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

/** ExecCommand 控制台输出在 GetOutputLog 中的分类名。*/
static const TCHAR* GExecLogCategory = TEXT("LogConsole");

/** 将 exec 捕获到的控制台输出镜像到日志缓冲区，供 GetOutputLog 查询。*/
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

void FNexusExecCommandCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ExecCommand");
	Out.Description = TEXT("Run UE console command and capture output. Mirrored to LogConsole.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Command"), FNexusSchema::Str(TEXT("Console command to execute")))
		.Prop(TEXT("bSilent"),  FNexusSchema::Bool(TEXT("Skip output capture"), false))
		.Prop(TEXT("Reason"),  FNexusSchema::Str(TEXT("Purpose for Confirm-mode gate. Required in Confirm mode.")))
		.Required({ TEXT("Command") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime, FNexusMcpTags::Dangerous };
	Out.ExtraSearchKeywords = { TEXT("console"), TEXT("cmd"), TEXT("cvar"), TEXT("stat"), TEXT("run") };
	Out.RelatedCapabilities = { TEXT("GetOutputLog") };
}

FCapabilityResult FNexusExecCommandCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Command = A.Str(TEXT("Command"));

		bool bSilent = false;
		Arguments->TryGetBoolField(TEXT("bSilent"), bSilent);

		if (!GEngine) { OutError = TEXT("GEngine unavailable"); return; }

		UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
		if (!World)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World()) { World = Ctx.World(); break; }
			}
		}

		const int32 LogMark = bSilent ? 0 : FNexusLogCapture::Get().GetTotalWritten();

		bool bExecResult = false;
		bool bFallbackUsed = false;
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
			UPlayer* Player = PC ? PC->Player : nullptr;
			if (Player)
			{
				bFallbackUsed = true;
				if (bSilent)
				{
					FOutputDeviceNull NullOut;
					bExecResult = Player->Exec(World, *Command, NullOut);
				}
				else
				{
					FNexusCapExecOutputDevice PcOut;
					bExecResult = Player->Exec(World, *Command, PcOut);
					if (DeviceOutput.IsEmpty() && !PcOut.Buffer.IsEmpty())
					{
						DeviceOutput = PcOut.Buffer;
					}
				}
			}
		}

		const FString Output = bSilent ? FString() : MergeExecOutput(DeviceOutput, LogMark);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("Command"),  Command);
		OutEntry->SetBoolField(TEXT("bExecuted"),   bExecResult);
		if (bFallbackUsed)
		{
			OutEntry->SetBoolField(TEXT("bFallbackUsed"), true);
		}
		if (!bSilent && !Output.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("Output"), Output);
			MirrorExecOutputToLogCapture(Command, Output);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusExecCommandCapability)
