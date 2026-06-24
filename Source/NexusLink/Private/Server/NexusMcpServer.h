// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"
#include "HttpRouteHandle.h"
// 前向声明（完整定义由 .cpp 引入）
class FNexusMcpDispatcher;
#include "IWebSocketServer.h"
#include "Containers/Ticker.h"

class IHttpRouter;
class IWebSocketServer;
class INetworkingWebSocket;

/**
 * NexusLink HTTP + WebSocket 服务器。
 * 基于 UE 内置模块，同时提供：
 *   1. POST /stream — MCP Streamable HTTP，供外部 AI 客户端直接连接（per-session 会话隔离）
 *   2. GET  /status — 无状态探测端点，返回项目信息
 *   3. WebSocket    — 供 Rider 插件长连接通信（JSON-RPC，无 MCP 握手）
 */
class NEXUSLINK_API FNexusMcpServer
{
public:
	FNexusMcpServer();
	~FNexusMcpServer();

	/** 启动 HTTP + WebSocket 监听。 */
	bool Start(int32 InMcpPort, int32 InWsPort);

	/** 停止服务器。 */
	void Stop();

	bool IsRunning() const { return bRunning; }
	int32 GetMcpPort() const { return McpPort; }
	int32 GetWsPort()  const { return WebSocketPort; }

	/**
	 * 向所有已连接的 WebSocket 客户端广播 JSON-RPC 通知。
	 * HTTP 会话为无状态 request-response，无法主动推送；客户端下次 tools/list 时自动获取最新列表。
	 */
	void BroadcastNotification(const FString& Method);

private:
	/** 注册 HTTP 路由。 */
	void RegisterRoutes();
	void UnregisterRoutes();

	/** 启动 WebSocket 服务器，返回是否成功。 */
	bool StartWebSocket();
	void StopWebSocket();

	/** WebSocket Ticker 回调（每帧 Tick WebSocket 服务器）。 */
	bool TickWebSocket(float DeltaTime);

	/** 会话主动清理 Ticker 回调（每 60s 扫描一次孤儿会话）。 */
	bool TickSessionCleanup(float DeltaTime);

	/** WebSocket 事件回调。 */
	void OnWebSocketClientConnected(INetworkingWebSocket* ClientWebSocket);
	void OnWebSocketMessage(void* Data, int32 DataSize, INetworkingWebSocket* ClientWebSocket);
	void OnWebSocketClientDisconnected(INetworkingWebSocket* ClientWebSocket);

	/**
	 * 按 Mcp-Session-Id 获取或创建 Dispatcher。
	 * initialize 请求时 SessionId 为空，创建新会话并生成 ID；后续请求按 ID 查找。
	 */
	TSharedPtr<FNexusMcpDispatcher> GetOrCreateDispatcher(const FString& SessionId, bool bIsInitialize, FString& OutSessionId);

	/** WebSocket 通道共享的无状态 Dispatcher。 */
	TSharedPtr<FNexusMcpDispatcher> WsDispatcher;

	/** HTTP 通道的 per-session Dispatcher 表（按 Mcp-Session-Id 索引）。 */
	TMap<FString, TSharedPtr<FNexusMcpDispatcher>> HttpSessions;

	TSharedPtr<IHttpRouter> HttpRouter;

	/** HTTP 路由句柄。 */
	FHttpRouteHandle StreamPostRouteHandle;
	FHttpRouteHandle StreamOptionsRouteHandle;
	FHttpRouteHandle StatusRouteHandle;

	/** WebSocket 服务器。 */
	TUniquePtr<IWebSocketServer> WebSocketServer;
	TArray<INetworkingWebSocket*> ConnectedClients;
#if NX_UE_HAS_FTSTICKER_HANDLE
	FTSTicker::FDelegateHandle WebSocketTickHandle;
	FTSTicker::FDelegateHandle SessionCleanupTickHandle;
#else
	FDelegateHandle WebSocketTickHandle;
	FDelegateHandle SessionCleanupTickHandle;
#endif

	int32 McpPort       = 0;
	int32 WebSocketPort = 0;
	bool  bRunning      = false;
};
