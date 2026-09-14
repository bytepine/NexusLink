// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FCapabilityResult;
class FJsonObject;
class UPackage;

/**
 * FNexusEditorTransaction::Begin() 返回的不透明事务句柄。
 * Runtime 侧只认这个接口；NexusLinkEditor 内部实现里包一层 FScopedTransaction（UnrealEd 类型，
 * 不能出现在 Runtime 模块的公开签名里）。
 */
class INexusTransactionHandle
{
public:
	virtual ~INexusTransactionHandle() = default;

	/** 还原内存快照后丢弃这笔 undo 记录（引擎 Cancel 本身不还原对象）。 */
	virtual void CancelAndRevert() = 0;

	/** 事务是否仍在栈顶未结束（供调用方判断是否真的回滚了）。 */
	virtual bool IsOutstanding() const = 0;
};

/**
 * NexusLink（Runtime）↔ NexusLinkEditor 之间仅剩的反向调用，用函数指针钩子解耦，
 * 避免 Runtime 模块直接链接 UnrealEd / Kismet2 / Settings 等编辑器专属引擎模块。
 *
 * 调用方（均驻留 Runtime 模块，Runtime cap 与 Editor cap 共用）：
 *   - FNexusCapability::Run（NexusCapability.cpp）              → ApplyManageFinalize / BeginTransaction
 *   - NexusMcpToolCallCapability（call_capability 元工具）        → BeginTransaction
 *   - FNexusDangerousCapGate::ConfirmOrDeny                     → PromptDangerousCapConfirm
 *   - FNexusPackageLedger::UnloadPackagesSafely                 → FlushPackages
 *   - FNexusLinkModule::TryStartMcpServer / StopMcpServer       → NotifyServerStarted / NotifyServerStopped
 *   - OpenSettingsPanel（当前无调用方，占位，供未来 UI 入口使用）
 *
 * 安装方：FNexusLinkEditorModule::StartupModule（NexusLinkEditor 模块，NexusInstallXxxHooks 系列调用）。
 * 未安装 / 已卸载时保持下方默认降级实现：安全跳过，不崩溃、不越权。
 */
struct NEXUSLINK_API FNexusEditorServices
{
	FNexusEditorServices() = delete;

	static TFunction<void(const FString& AssetPath, bool bCompile, bool bSaveToDisk, TSharedPtr<FJsonObject>& OutTop)> ApplyManageFinalize;
	static TFunction<TUniquePtr<INexusTransactionHandle>(const FString& Title)> BeginTransaction;
	static TFunction<bool()> IsTransactionActive;
	static TFunction<int32()> GetActiveTransactionRecordCount;
	static TFunction<FCapabilityResult(const FString& CapName, const FString& Reason, const FString& Payload)> PromptDangerousCapConfirm;
	/** 返回实际卸载数量；OutSkipped 非空时回填被跳过的包。 */
	static TFunction<int32(const TArray<UPackage*>& Packages, bool bSkipDirty, bool bGC, TSet<UPackage*>* OutSkipped)> FlushPackages;
	static TFunction<void()> OpenSettingsPanel;
	/** MCP 服务器已在给定端口启动；供状态栏等 Editor UI 响应。未安装钩子时空操作。 */
	static TFunction<void(int32 McpPort, int32 WsPort)> NotifyServerStarted;
	/** MCP 服务器已停止；供状态栏等 Editor UI 响应。未安装钩子时空操作。 */
	static TFunction<void()> NotifyServerStopped;
};
