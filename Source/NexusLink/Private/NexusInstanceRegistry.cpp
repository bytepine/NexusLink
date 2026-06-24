// Copyright byteyang. All Rights Reserved.

#include "NexusInstanceRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusRegistry, Log, All);

/** 注册目录：{TempDir}/NexusLink/ */
static FString GetRegistryDir()
{
	return FPaths::Combine(FString(FPlatformProcess::UserTempDir()), TEXT("NexusLink"));
}

/** 本进程的注册文件路径：{RegistryDir}/{PID}.json */
static FString GetRegistryFilePath()
{
	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	return FPaths::Combine(GetRegistryDir(), FString::Printf(TEXT("%u.json"), Pid));
}

/** 扫描注册目录，删除 PID 已不存活的残留文件。 */
static void CleanupStaleFiles()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString Dir = GetRegistryDir();

	TArray<FString> Files;
	PlatformFile.FindFiles(Files, *Dir, TEXT("json"));

	for (const FString& FilePath : Files)
	{
		const FString FileName = FPaths::GetBaseFilename(FilePath);
		if (!FileName.IsNumeric())
		{
			continue;
		}

		const uint32 Pid = static_cast<uint32>(FCString::Atoi64(*FileName));
		if (!FPlatformProcess::IsApplicationRunning(Pid))
		{
			PlatformFile.DeleteFile(*FilePath);
			UE_LOG(LogNexusRegistry, Log, TEXT("已清理失效实例文件（PID %u 已退出）：%s"), Pid, *FilePath);
		}
	}
}

void FNexusInstanceRegistry::Register(int32 McpPort, int32 WsPort, const FString& ProjectName, const FString& EngineVersion)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString Dir = GetRegistryDir();

	if (!PlatformFile.DirectoryExists(*Dir))
	{
		PlatformFile.CreateDirectoryTree(*Dir);
	}

	// 清理已退出进程的残留文件（处理上次崩溃未能执行 Unregister 的情况）
	CleanupStaleFiles();

	// 对字段值做基础 JSON 转义（项目名通常为标识符，此处作健壮性处理）
	auto EscapeJson = [](const FString& Str) -> FString
	{
		return Str.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	};

	const FString Json = FString::Printf(
		TEXT("{\"pid\":%u,\"mcpPort\":%d,\"wsPort\":%d,\"projectName\":\"%s\",\"engineVersion\":\"%s\"}"),
		FPlatformProcess::GetCurrentProcessId(),
		McpPort,
		WsPort,
		*EscapeJson(ProjectName),
		*EscapeJson(EngineVersion)
	);

	const FString FilePath = GetRegistryFilePath();
	if (FFileHelper::SaveStringToFile(Json, *FilePath))
	{
		UE_LOG(LogNexusRegistry, Log, TEXT("实例已注册：MCP=%d, WS=%d → %s"), McpPort, WsPort, *FilePath);
	}
	else
	{
		UE_LOG(LogNexusRegistry, Warning, TEXT("实例注册失败，无法写入文件：%s"), *FilePath);
	}
}

void FNexusInstanceRegistry::Unregister()
{
	const FString FilePath = GetRegistryFilePath();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*FilePath))
	{
		PlatformFile.DeleteFile(*FilePath);
		UE_LOG(LogNexusRegistry, Log, TEXT("实例注册已注销：%s"), *FilePath);
	}
}

TArray<int32> FNexusInstanceRegistry::GetClaimedPorts()
{
	TArray<int32> Result;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString Dir = GetRegistryDir();
	const uint32 MyPid = FPlatformProcess::GetCurrentProcessId();

	if (!PlatformFile.DirectoryExists(*Dir))
	{
		return Result;
	}

	TArray<FString> Files;
	PlatformFile.FindFiles(Files, *Dir, TEXT("json"));

	for (const FString& FilePath : Files)
	{
		const FString FileName = FPaths::GetBaseFilename(FilePath);
		if (!FileName.IsNumeric()) continue;

		const uint32 Pid = static_cast<uint32>(FCString::Atoi64(*FileName));
		if (Pid == MyPid || !FPlatformProcess::IsApplicationRunning(Pid)) continue;

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FilePath)) continue;

		TSharedPtr<FJsonObject> Json;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid()) continue;

		if (Json->HasField(TEXT("mcpPort")))
		{
			Result.AddUnique(static_cast<int32>(Json->GetNumberField(TEXT("mcpPort"))));
		}
		if (Json->HasField(TEXT("wsPort")))
		{
			Result.AddUnique(static_cast<int32>(Json->GetNumberField(TEXT("wsPort"))));
		}
	}

	UE_LOG(LogNexusRegistry, Log, TEXT("已发现 %d 个被其他实例占用的端口"), Result.Num());
	return Result;
}
