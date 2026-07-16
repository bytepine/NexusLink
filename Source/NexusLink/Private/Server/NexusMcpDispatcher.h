// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"

struct FNexusMcpToolResult;

/**
 * MCP 会话初始化状态。
 */
enum class ENexusMcpSessionState : uint8
{
	/** 等待 initialize 请求。 */
	WaitingForInitialize,
	/** 已收到 initialize，等待 initialized 通知。 */
	WaitingForInitialized,
	/** 正常运行。 */
	Running,
	/** 已关闭。 */
	Closed
};

/**
 * MCP JSON-RPC 分发器。
 * 无网络依赖，负责解析 JSON-RPC 消息并路由到对应的 MCP 方法处理函数。
 * 处理结果通过 OnSendResponse 委托回传给上层网络层发送。
 */
class NEXUSLINK_API FNexusMcpDispatcher
{
public:
	/** 发送响应的回调，上层网络层实现具体发送逻辑。 */
	using FOnSendResponse = TFunction<void(const FString& JsonResponse)>;

	explicit FNexusMcpDispatcher(FOnSendResponse InSendCallback);

	/**
	 * 处理一条原始 JSON 字符串（单条 JSON-RPC 消息）。
	 * @param JsonLine       JSON-RPC 消息字符串。
	 * @param PerRequestSend 本次请求的响应发送回调，临时覆盖构造时传入的默认回调。
	 */
	void Dispatch(const FString& JsonLine, FOnSendResponse PerRequestSend);

	/**
	 * 无状态直接分发（供 WebSocket 通道使用）。
	 * 跳过 MCP 握手，直接处理 tools/list、tools/call、ping。
	 * @param JsonLine       JSON-RPC 消息字符串。
	 * @param PerRequestSend 响应回调。
	 */
	void DispatchDirect(const FString& JsonLine, FOnSendResponse PerRequestSend);

	ENexusMcpSessionState GetState() const { return State; }
	bool IsRunning() const { return State == ENexusMcpSessionState::Running; }

	/** 会话创建时刻（UTC）。 */
	FDateTime GetCreatedAt() const { return CreatedAt; }

	/** 最近一次活动时刻（收到任意请求时更新，UTC）。 */
	FDateTime GetLastActivityAt() const { return LastActivityAt; }

	/** 设置当前会话 ID，仅供反馈采集器关联事件来源；为空表示无会话上下文（如 WebSocket）。 */
	void SetSessionId(const FString& InSessionId) { SessionId = InSessionId; }

private:
	void SendResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
	void SendError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);

	/** 将 FNexusMcpToolResult 统一序列化并发送给客户端（供 MultiTool 模式复用）。 */
	void EmitToolResult(const TSharedPtr<FJsonValue>& Id, const FNexusMcpToolResult& ToolResult);

	void HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);
	void HandleInitialized();
	void HandleToolsList(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);
	void HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);

	/** 处理 IDE/Desktop 代理转发失败上报（nexus/proxy_feedback），落盘为 RecordAuto。 */
	void HandleProxyFeedback(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);

	FOnSendResponse SendCallback;
	ENexusMcpSessionState State = ENexusMcpSessionState::WaitingForInitialize;

	/** 会话创建时刻（UTC）。 */
	FDateTime CreatedAt = FDateTime::UtcNow();

	/** 最近一次活动时刻（收到任意请求时更新）。 */
	FDateTime LastActivityAt = FDateTime::UtcNow();

	/** initialize 握手中协商的协议版本。 */
	FString ProtocolVersion;

	/** 当前会话 ID（HTTP 路径设置；WebSocket 路径为空字符串）。 */
	FString SessionId;
};
