// Copyright byteyang. All Rights Reserved.

#include "NexusMcpToolRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusMcpRegistry, Log, All);

FNexusMcpToolRegistry& FNexusMcpToolRegistry::Get()
{
	static FNexusMcpToolRegistry Instance;
	return Instance;
}

void FNexusMcpToolRegistry::RegisterTool(const FString& Name, FNexusMcpToolFactory Factory, const FNexusMcpToolDefinition& Definition)
{
	if (ToolFactories.Contains(Name))
	{
		UE_LOG(LogNexusMcpRegistry, Warning, TEXT("MCP Tool '%s' already registered, overwriting"), *Name);
		CachedDefinitions.RemoveAll([&Name](const FNexusMcpToolDefinition& Def) { return Def.Name == Name; });
	}
	ToolFactories.Add(Name, MoveTemp(Factory));
	CachedDefinitions.Add(Definition);
	UE_LOG(LogNexusMcpRegistry, Log, TEXT("Registered MCP Tool: %s"), *Name);
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
