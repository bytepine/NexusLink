// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMcpTool.h"

/** 工具工厂函数类型，创建并返回一个新的工具实例。 */
using FNexusMcpToolFactory = TFunction<TSharedPtr<FNexusMcpTool>()>;

/**
 * MCP 工具注册表 —— 全局单例。
 * 管理所有已注册的 MCP Tool，供会话层查询和调用。
 * Definition 在注册时缓存，避免每次 tools/list 重复创建临时实例。
 */
class NEXUSLINK_API FNexusMcpToolRegistry
{
public:
	static FNexusMcpToolRegistry& Get();

	/** 以指定名称注册一个工具工厂和缓存定义。 */
	void RegisterTool(const FString& Name, FNexusMcpToolFactory Factory, const FNexusMcpToolDefinition& Definition);

	/** 返回所有已注册工具的定义列表（直接返回缓存，零分配）。 */
	const TArray<FNexusMcpToolDefinition>& GetAllDefinitions() const;

	/** 按名称创建工具实例，未找到时返回 nullptr。 */
	TSharedPtr<FNexusMcpTool> CreateTool(const FString& Name) const;

	/** 检查指定名称的工具是否已注册。 */
	bool HasTool(const FString& Name) const;

	/**
	 * 静态初始化期收集的诊断信息（重复注册等）。
	 * 注册期禁止 UE_LOG，故延迟到模块启动后再输出。
	 */
	const TArray<FString>& GetPendingWarnings() const;

private:
	TMap<FString, FNexusMcpToolFactory> ToolFactories;
	TArray<FNexusMcpToolDefinition> CachedDefinitions;
	TArray<FString> PendingWarnings;
};

/**
 * 静态初始化期自动注册辅助类。
 *
 * 约束：构造函数运行于 dyld / CRT 静态初始化阶段，此时 GMalloc、TLS、Trace 尚未完全就绪，
 * 严禁调用 UE_LOG / ensureMsgf / CPU Profiler 相关宏（iOS 上会直接 EXC_BAD_ACCESS）。
 */
struct FNexusMcpToolAutoRegister
{
	FNexusMcpToolAutoRegister(const FNexusMcpToolDefinition& Definition, FNexusMcpToolFactory Factory)
	{
		FNexusMcpToolRegistry::Get().RegisterTool(Definition.Name, MoveTemp(Factory), Definition);
	}
};

/**
 * 自动注册宏。
 * 用法：在 Tool 实现 .cpp 文件末尾添加：
 *   REGISTER_MCP_TOOL(FMyTool)
 *
 * 要求 FMyTool 有默认构造函数且继承自 FNexusMcpTool。
 * 注册时缓存 Definition，后续 tools/list 零开销。
 *
 * 非编辑器构建（Game / Client / Server / Shipping）中 MCP 永不启动
 * （见 FNexusLinkModule::StartupModule 的 !WITH_EDITOR 分支），
 * 故宏整体编译为空：避免游戏包在静态初始化期做任何分配与日志。
 */
#if WITH_EDITOR
#define REGISTER_MCP_TOOL(ToolClass) \
	static FNexusMcpToolAutoRegister AutoRegister_##ToolClass( \
		ToolClass().GetDefinition(), \
		[]() -> TSharedPtr<FNexusMcpTool> { return MakeShared<ToolClass>(); } \
	);
#else
#define REGISTER_MCP_TOOL(ToolClass)
#endif
