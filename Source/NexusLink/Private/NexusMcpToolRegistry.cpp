// Copyright byteyang. All Rights Reserved.

#include "NexusMcpToolRegistry.h"

// 注意：RegisterTool 由 REGISTER_MCP_TOOL 在全局静态初始化期调用（早于 GMalloc / TLS / Trace 完全就绪）。
// 此处严禁使用 UE_LOG / ensureMsgf 等宏：FMsg::Logf_Internal 会触发 CPU Profiler 的 TLS 缓冲分配，
// 在 iOS 等平台上表现为启动瞬间 EXC_BAD_ACCESS 崩溃。诊断信息延迟到 GetPendingWarnings() 由模块启动时输出。

FNexusMcpToolRegistry& FNexusMcpToolRegistry::Get()
{
	static FNexusMcpToolRegistry Instance;
	return Instance;
}

void FNexusMcpToolRegistry::RegisterTool(const FString& Name, FNexusMcpToolFactory Factory, const FNexusMcpToolDefinition& Definition)
{
	if (ToolFactories.Contains(Name))
	{
		PendingWarnings.Add(FString::Printf(TEXT("MCP Tool '%s' already registered, overwriting"), *Name));
		CachedDefinitions.RemoveAll([&Name](const FNexusMcpToolDefinition& Def) { return Def.Name == Name; });
	}
	ToolFactories.Add(Name, MoveTemp(Factory));
	CachedDefinitions.Add(Definition);
}

const TArray<FString>& FNexusMcpToolRegistry::GetPendingWarnings() const
{
	return PendingWarnings;
}

const TArray<FNexusMcpToolDefinition>& FNexusMcpToolRegistry::GetAllDefinitions() const
{
	return CachedDefinitions;
}

TSharedPtr<FNexusMcpTool> FNexusMcpToolRegistry::CreateTool(const FString& Name) const
{
	const FNexusMcpToolFactory* Factory = ToolFactories.Find(Name);
	if (Factory)
	{
		return (*Factory)();
	}
	return nullptr;
}

bool FNexusMcpToolRegistry::HasTool(const FString& Name) const
{
	return ToolFactories.Contains(Name);
}
