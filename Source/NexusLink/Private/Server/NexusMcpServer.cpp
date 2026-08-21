// Copyright byteyang. All Rights Reserved.

#include "Server/NexusMcpServer.h"
#include "Server/NexusMcpDispatcher.h"
#include "Utils/NexusVersionCompat.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpPath.h"
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "INetworkingWebSocket.h"
#include "Misc/App.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Utils/NexusJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#include "Misc/Guid.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusMcpServer, Log, All);

static const FString StreamEndpoint  = TEXT("/stream");
static const FString StatusEndpoint  = TEXT("/status");
static const FString McpSessionHeader = TEXT("Mcp-Session-Id");
static const int32 MaxMcpBodyBytes = 1024 * 1024;

static FString GenerateAuthToken()
{
	return FGuid::NewGuid().ToString(EGuidFormats::Digits)
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

static bool TokensEqual(const FString& A, const FString& B)
{
	if (A.Len() != B.Len())
	{
		return false;
	}
	int32 Acc = 0;
	for (int32 i = 0; i < A.Len(); ++i)
	{
		Acc |= static_cast<int32>(A[i]) ^ static_cast<int32>(B[i]);
	}
	return Acc == 0;
}

/**
 * 探测当前进程的网络角色：PIE/Game 世界存在时返回 DedicatedServer/ListenServer/Client/Standalone，
 * 否则返回 "Editor"（编辑器空闲）或 "Unknown"。
 * 供 /status 与 WS info 端点复用，使外层代理能把多实例并发请求按角色定位到目标端口。
 */
static FString DetectCurrentNetRole()
{
	check(IsInGameThread());
	if (!GEngine) { return TEXT("Unknown"); }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		UWorld* World = Ctx.World();
		if (!World) continue;
		if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
		switch (World->GetNetMode())
		{
			case NM_DedicatedServer: return TEXT("DedicatedServer");
			case NM_ListenServer:    return TEXT("ListenServer");
			case NM_Client:          return TEXT("Client");
			case NM_Standalone:      return TEXT("Standalone");
			default:                 break;
		}
	}
	return TEXT("Editor");
}

// UE 5.4 将 FHttpRequestHandler 从 TFunction 改为 TDelegate
#if NX_UE_HAS_HTTP_DELEGATE
template<typename F>
static FHttpRequestHandler MakeHttpHandler(F Fn) { return FHttpRequestHandler::CreateLambda(MoveTemp(Fn)); }
#else
template<typename F>
static FHttpRequestHandler MakeHttpHandler(F Fn) { return MoveTemp(Fn); }
#endif

static void ReplyJson(const FHttpResultCallback& OnComplete, const FString& Json, const FString& SessionId = FString())
{
	auto Response = FHttpServerResponse::Create(Json, TEXT("application/json"));
	if (!SessionId.IsEmpty())
	{
		Response->Headers.Add(McpSessionHeader, { SessionId });
	}
	OnComplete(MoveTemp(Response));
}

static void ReplyError(const FHttpResultCallback& OnComplete, EHttpServerResponseCodes Code,
	const TCHAR* ErrorCode, const TCHAR* Message)
{
	auto ErrResponse = FHttpServerResponse::Error(Code, ErrorCode, Message);
	OnComplete(MoveTemp(ErrResponse));
}

FNexusMcpServer::FNexusMcpServer()
{
}

FNexusMcpServer::~FNexusMcpServer()
{
	Stop();
}

bool FNexusMcpServer::Start(int32 InMcpPort, int32 InWsPort)
{
	if (bRunning)
	{
		UE_LOG(LogNexusMcpServer, Warning, TEXT("服务器已在端口 %d 运行"), McpPort);
		return true;
	}

	McpPort       = InMcpPort;
	WebSocketPort = InWsPort;
	AuthToken     = GenerateAuthToken();

	if (GConfig)
	{
		FString CurrentBind;
		GConfig->GetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"), CurrentBind, GEngineIni);
		if (!CurrentBind.IsEmpty()
			&& !CurrentBind.Equals(TEXT("127.0.0.1"), ESearchCase::IgnoreCase)
			&& !CurrentBind.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogNexusMcpServer, Warning,
				TEXT("覆盖 HTTP DefaultBindAddress=%s → 127.0.0.1"), *CurrentBind);
		}
		GConfig->SetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"), TEXT("127.0.0.1"), GEngineIni);
	}

	HttpRouter = FHttpServerModule::Get().GetHttpRouter(static_cast<uint32>(McpPort));
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogNexusMcpServer, Error, TEXT("无法在端口 %d 获取 HttpRouter"), McpPort);
		return false;
	}

	WsDispatcher = MakeShared<FNexusMcpDispatcher>(FNexusMcpDispatcher::FOnSendResponse{});

	RegisterRoutes();
	FHttpServerModule::Get().StartAllListeners();

	// WebSocket 启动失败则回滚 HTTP 监听
	if (!StartWebSocket())
	{
		UnregisterRoutes();
		WsDispatcher.Reset();
		FHttpServerModule::Get().StopAllListeners();
		HttpRouter.Reset();
		return false;
	}

	bRunning = true;

	// 注册 60s 周期的会话主动清理 Ticker，防止客户端崩溃后孤儿会话滞留
#if NX_UE_HAS_FTSTICKER
	SessionCleanupTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FNexusMcpServer::TickSessionCleanup), 60.0f);
#else
	SessionCleanupTickHandle = FTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FNexusMcpServer::TickSessionCleanup), 60.0f);
#endif

	UE_LOG(LogNexusMcpServer, Log,
		TEXT("NexusLink 服务器已启动\n  Streamable HTTP : http://127.0.0.1:%d/stream\n  Status          : http://127.0.0.1:%d/status\n  WebSocket       : ws://127.0.0.1:%d/"),
		McpPort, McpPort, WebSocketPort);
	return true;
}

void FNexusMcpServer::Stop()
{
	if (!bRunning)
	{
		return;
	}

	bRunning = false;
	StopWebSocket();

	// 移除会话清理 Ticker
	if (SessionCleanupTickHandle.IsValid())
	{
#if NX_UE_HAS_FTSTICKER
		FTSTicker::GetCoreTicker().RemoveTicker(SessionCleanupTickHandle);
#else
		FTicker::GetCoreTicker().RemoveTicker(SessionCleanupTickHandle);
#endif
		SessionCleanupTickHandle.Reset();
	}

	UnregisterRoutes();
	HttpSessions.Empty();
	AuthenticatedWsClients.Empty();
	AuthToken.Empty();
	WsDispatcher.Reset();

	if (FHttpServerModule::IsAvailable())
	{
		FHttpServerModule::Get().StopAllListeners();
	}

	HttpRouter.Reset();
	UE_LOG(LogNexusMcpServer, Log, TEXT("NexusLink 服务器已停止"));
}

/** 从 Request.Headers 提取指定头的第一个值（大小写不敏感），不存在时返回空字符串。 */
static FString GetRequestHeader(const FHttpServerRequest& Request, const FString& HeaderName)
{
	for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
	{
		if (Pair.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
		{
			return Pair.Value[0];
		}
	}
	return FString();
}

static bool ExtractBearerToken(const FString& AuthHeader, FString& OutToken)
{
	const FString Prefix = TEXT("Bearer ");
	if (!AuthHeader.StartsWith(Prefix, ESearchCase::IgnoreCase))
	{
		return false;
	}
	OutToken = AuthHeader.Mid(Prefix.Len()).TrimStartAndEnd();
	return !OutToken.IsEmpty();
}

void FNexusMcpServer::RegisterRoutes()
{
	if (!HttpRouter.IsValid())
	{
		return;
	}

	// POST /stream — MCP Streamable HTTP（per-session 会话隔离）
	// 收包线程只拷贝 body/header，会话表与 Dispatch 一律回切 GameThread；OnComplete 推迟到派发结束，避免 FEvent::Wait 卡死收包线程。
	const TWeakPtr<FNexusMcpServer> WeakSelf = AsShared();
	StreamPostRouteHandle = HttpRouter->BindRoute(
		FHttpPath(StreamEndpoint),
		EHttpServerRequestVerbs::VERB_POST,
		MakeHttpHandler([WeakSelf](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
		{
			if (!GetRequestHeader(Request, TEXT("Origin")).IsEmpty())
			{
				ReplyError(OnComplete, EHttpServerResponseCodes::Denied,
					TEXT("origin_forbidden"), TEXT("Browser Origin is not allowed"));
				return true;
			}

			if (Request.Body.Num() > MaxMcpBodyBytes)
			{
				ReplyError(OnComplete, EHttpServerResponseCodes::BadRequest,
					TEXT("payload_too_large"), TEXT("Request body exceeds 1MB"));
				return true;
			}

			FString PresentedToken;
			if (!ExtractBearerToken(GetRequestHeader(Request, TEXT("Authorization")), PresentedToken))
			{
				ReplyError(OnComplete, EHttpServerResponseCodes::Denied,
					TEXT("unauthorized"), TEXT("Missing Authorization: Bearer token"));
				return true;
			}

			FString JsonBody;
			if (Request.Body.Num() > 0)
			{
				FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
				JsonBody = FString(Converter.Length(), Converter.Get());
			}

			if (JsonBody.IsEmpty())
			{
				ReplyError(OnComplete, EHttpServerResponseCodes::BadRequest,
					TEXT("empty_body"), TEXT("Request body is empty"));
				return true;
			}

			// 解析 JSON 后检查 method 字段，避免参数值中含 "initialize" 导致误判
			const FString IncomingSessionId = GetRequestHeader(Request, McpSessionHeader);
			bool bIsInitialize = false;
			{
				TSharedPtr<FJsonObject> PreParsed;
				TSharedRef<TJsonReader<>> PreReader = TJsonReaderFactory<>::Create(JsonBody);
				if (FJsonSerializer::Deserialize(PreReader, PreParsed) && PreParsed.IsValid())
				{
					FString PreMethod;
					PreParsed->TryGetStringField(TEXT("method"), PreMethod);
					bIsInitialize = (PreMethod == TEXT("initialize"));
				}
			}

			FHttpResultCallback Complete = OnComplete;
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, JsonBody, IncomingSessionId, bIsInitialize, PresentedToken, Complete]()
			{
				const TSharedPtr<FNexusMcpServer> Server = WeakSelf.Pin();
				if (!Server.IsValid() || !Server->IsRunning())
				{
					ReplyError(Complete, EHttpServerResponseCodes::ServerError,
						TEXT("server_stopped"), TEXT("MCP server is stopping"));
					return;
				}

				if (!TokensEqual(PresentedToken, Server->GetAuthToken()))
				{
					ReplyError(Complete, EHttpServerResponseCodes::Denied,
						TEXT("unauthorized"), TEXT("Invalid Authorization token"));
					return;
				}

				FString SessionId;
				TSharedPtr<FNexusMcpDispatcher> Dispatcher =
					Server->GetOrCreateDispatcher(IncomingSessionId, bIsInitialize, SessionId);
				if (!Dispatcher.IsValid())
				{
					ReplyError(Complete, EHttpServerResponseCodes::NotFound,
						TEXT("session_not_found"), TEXT("Invalid or missing Mcp-Session-Id"));
					return;
				}

				FString ResponseJson;
				Dispatcher->Dispatch(JsonBody, [&ResponseJson](const FString& Json)
				{
					ResponseJson = Json;
				});
				ReplyJson(Complete, ResponseJson, SessionId);
			});
			return true;
		})
	);

	// OPTIONS /stream — 不再提供 CORS 预检（MCP 非浏览器客户端）
	StreamOptionsRouteHandle.Reset();

	// GET /status — 无状态探测；WorldContexts / NetMode 只能在 GameThread 读；不含 token
	StatusRouteHandle = HttpRouter->BindRoute(
		FHttpPath(StatusEndpoint),
		EHttpServerRequestVerbs::VERB_GET,
		MakeHttpHandler([WeakSelf](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
		{
			FHttpResultCallback Complete = OnComplete;
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Complete]()
			{
				const TSharedPtr<FNexusMcpServer> Server = WeakSelf.Pin();
				if (!Server.IsValid() || !Server->IsRunning())
				{
					ReplyError(Complete, EHttpServerResponseCodes::ServerError,
						TEXT("server_stopped"), TEXT("MCP server is stopping"));
					return;
				}

				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("server"), TEXT("Nexus-Unreal"));
				Obj->SetStringField(TEXT("version"), TEXT("0.0.0"));
				Obj->SetStringField(TEXT("engineVersion"),
					FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION));
				Obj->SetStringField(TEXT("projectName"), FApp::GetProjectName());
				Obj->SetNumberField(TEXT("wsPort"), Server->GetWsPort());
				Obj->SetStringField(TEXT("netRole"), DetectCurrentNetRole());
				Obj->SetBoolField(TEXT("authRequired"), true);
				ReplyJson(Complete, FNexusJsonUtils::SerializeCondensed(Obj));
			});
			return true;
		})
	);
}

void FNexusMcpServer::UnregisterRoutes()
{
	if (!HttpRouter.IsValid())
	{
		return;
	}

	if (StreamPostRouteHandle)    { HttpRouter->UnbindRoute(StreamPostRouteHandle);    StreamPostRouteHandle.Reset(); }
	if (StreamOptionsRouteHandle) { HttpRouter->UnbindRoute(StreamOptionsRouteHandle); StreamOptionsRouteHandle.Reset(); }
	if (StatusRouteHandle)        { HttpRouter->UnbindRoute(StatusRouteHandle);        StatusRouteHandle.Reset(); }
}

TSharedPtr<FNexusMcpDispatcher> FNexusMcpServer::GetOrCreateDispatcher(
	const FString& SessionId, bool bIsInitialize, FString& OutSessionId)
{
	check(IsInGameThread());
	if (bIsInitialize)
	{
		// initialize 请求：创建新会话
		OutSessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		TSharedPtr<FNexusMcpDispatcher> NewDispatcher =
			MakeShared<FNexusMcpDispatcher>(FNexusMcpDispatcher::FOnSendResponse{});
		NewDispatcher->SetSessionId(OutSessionId);
		HttpSessions.Add(OutSessionId, NewDispatcher);

		UE_LOG(LogNexusMcpServer, Log, TEXT("新建 MCP 会话: %s（当前 %d 个活跃会话）"),
			*OutSessionId, HttpSessions.Num());
		return NewDispatcher;
	}

	// 非 initialize 请求：按 SessionId 查找
	if (SessionId.IsEmpty())
	{
		UE_LOG(LogNexusMcpServer, Warning, TEXT("POST /stream 缺少 Mcp-Session-Id header"));
		return nullptr;
	}

	TSharedPtr<FNexusMcpDispatcher>* Found = HttpSessions.Find(SessionId);
	if (!Found || !Found->IsValid())
	{
		UE_LOG(LogNexusMcpServer, Warning, TEXT("未知 Mcp-Session-Id: %s"), *SessionId);
		return nullptr;
	}

	OutSessionId = SessionId;
	return *Found;
}

// --- WebSocket ---

bool FNexusMcpServer::StartWebSocket()
{
	IWebSocketNetworkingModule* WsModule = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!WsModule)
	{
		UE_LOG(LogNexusMcpServer, Error, TEXT("WebSocketNetworking 模块加载失败"));
		return false;
	}

	WebSocketServer = WsModule->CreateServer();
	if (!WebSocketServer.IsValid())
	{
		UE_LOG(LogNexusMcpServer, Error, TEXT("WebSocket 服务器创建失败"));
		return false;
	}

	FWebSocketClientConnectedCallBack OnConnected;
	OnConnected.BindRaw(this, &FNexusMcpServer::OnWebSocketClientConnected);

	const int32 WsPort = WebSocketPort;
	if (!WebSocketServer->Init(static_cast<uint32>(WsPort), OnConnected))
	{
		UE_LOG(LogNexusMcpServer, Error, TEXT("WebSocket 服务器初始化失败，端口: %d"), WsPort);
		WebSocketServer.Reset();
		return false;
	}

#if NX_UE_HAS_FTSTICKER
	WebSocketTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FNexusMcpServer::TickWebSocket), 0.0f);
#else
	WebSocketTickHandle = FTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FNexusMcpServer::TickWebSocket), 0.0f);
#endif

	UE_LOG(LogNexusMcpServer, Log, TEXT("WebSocket 服务器已启动，端口: %d（引擎 Init 不暴露绑定地址；须 WS 首帧 auth）"), WsPort);
	return true;
}

void FNexusMcpServer::StopWebSocket()
{
	if (WebSocketTickHandle.IsValid())
	{
#if NX_UE_HAS_FTSTICKER
		FTSTicker::GetCoreTicker().RemoveTicker(WebSocketTickHandle);
#else
		FTicker::GetCoreTicker().RemoveTicker(WebSocketTickHandle);
#endif
		WebSocketTickHandle.Reset();
	}
	ConnectedClients.Empty();
	AuthenticatedWsClients.Empty();
	WebSocketServer.Reset();
}

void FNexusMcpServer::OnWebSocketClientConnected(INetworkingWebSocket* ClientWebSocket)
{
	UE_LOG(LogNexusMcpServer, Verbose, TEXT("WebSocket 客户端已连接"));
	ConnectedClients.Add(ClientWebSocket);

	// 绑定消息和断开回调
	FWebSocketPacketReceivedCallBack OnMessage;
	OnMessage.BindRaw(this, &FNexusMcpServer::OnWebSocketMessage, ClientWebSocket);
	ClientWebSocket->SetReceiveCallBack(OnMessage);

	FWebSocketInfoCallBack OnDisconnected;
	OnDisconnected.BindLambda([this, ClientWebSocket]()
	{
		OnWebSocketClientDisconnected(ClientWebSocket);
	});
	ClientWebSocket->SetSocketClosedCallBack(OnDisconnected);
}

static void SendWsText(INetworkingWebSocket* ClientWebSocket, const FString& Json)
{
	if (!ClientWebSocket || Json.IsEmpty())
	{
		return;
	}
	FTCHARToUTF8 Utf8Converter(*Json);
	ClientWebSocket->Send(
		reinterpret_cast<const uint8*>(Utf8Converter.Get()),
		Utf8Converter.Length(),
		/*bPrependSize=*/false);
}

static FString MakeJsonRpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
	TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (Id.IsValid())
	{
		Msg->SetField(TEXT("id"), Id);
	}
	Msg->SetObjectField(TEXT("result"), Result);
	return FNexusJsonUtils::SerializeCondensed(Msg);
}

static FString MakeJsonRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetNumberField(TEXT("code"), Code);
	Err->SetStringField(TEXT("message"), Message);
	TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (Id.IsValid())
	{
		Msg->SetField(TEXT("id"), Id);
	}
	Msg->SetObjectField(TEXT("error"), Err);
	return FNexusJsonUtils::SerializeCondensed(Msg);
}

void FNexusMcpServer::OnWebSocketMessage(void* Data, int32 DataSize, INetworkingWebSocket* ClientWebSocket)
{
	if (!WsDispatcher.IsValid() || !Data || DataSize <= 0 || !ClientWebSocket)
	{
		return;
	}

	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), DataSize);
	FString JsonLine(Converter.Length(), Converter.Get());

	// 勿在 WebSocket 收包回调里同步执行 tools/call：search_asset 等会阻塞 GameThread 数秒～数十秒，
	// 同帧内无法继续 TickWebSocket，代理侧长连接易被判定超时/断开。推迟到本帧后续 GameThread 任务执行。
	AsyncTask(ENamedThreads::GameThread, [this, ClientWebSocket, JsonLine = MoveTemp(JsonLine)]()
	{
		if (!ConnectedClients.Contains(ClientWebSocket) || !WsDispatcher.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> JsonMsg;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
		TSharedPtr<FJsonValue> Id;
		FString Method;
		if (FJsonSerializer::Deserialize(Reader, JsonMsg) && JsonMsg.IsValid())
		{
			JsonMsg->TryGetStringField(TEXT("method"), Method);
			if (JsonMsg->HasField(TEXT("id")))
			{
				Id = JsonMsg->TryGetField(TEXT("id"));
			}
		}

		if (Method == TEXT("auth"))
		{
			FString Presented;
			const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
			if (JsonMsg.IsValid() && JsonMsg->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj)
			{
				(*ParamsObj)->TryGetStringField(TEXT("token"), Presented);
			}
			if (TokensEqual(Presented, AuthToken))
			{
				AuthenticatedWsClients.Add(ClientWebSocket);
				TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
				Ok->SetBoolField(TEXT("ok"), true);
				SendWsText(ClientWebSocket, MakeJsonRpcResult(Id, Ok));
			}
			else
			{
				SendWsText(ClientWebSocket, MakeJsonRpcError(Id, -32001, TEXT("unauthorized")));
			}
			return;
		}

		if (!AuthenticatedWsClients.Contains(ClientWebSocket) && Method != TEXT("ping"))
		{
			SendWsText(ClientWebSocket, MakeJsonRpcError(Id, -32001, TEXT("unauthorized")));
			return;
		}

		FString ResponseJson;
		WsDispatcher->DispatchDirect(JsonLine, [&ResponseJson](const FString& Json)
		{
			ResponseJson = Json;
		});
		SendWsText(ClientWebSocket, ResponseJson);
	});
}

void FNexusMcpServer::OnWebSocketClientDisconnected(INetworkingWebSocket* ClientWebSocket)
{
	UE_LOG(LogNexusMcpServer, Verbose, TEXT("WebSocket 客户端已断开"));
	AuthenticatedWsClients.Remove(ClientWebSocket);
	ConnectedClients.Remove(ClientWebSocket);
}

bool FNexusMcpServer::TickWebSocket(float DeltaTime)
{
	if (WebSocketServer.IsValid())
	{
		WebSocketServer->Tick();
	}
	return true; // 返回 true 保持 Ticker 持续运行
}

bool FNexusMcpServer::TickSessionCleanup(float /*DeltaTime*/)
{
	check(IsInGameThread());
	const FDateTime Now = FDateTime::UtcNow();
	int32 Removed = 0;
	for (auto It = HttpSessions.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid()) { It.RemoveCurrent(); ++Removed; continue; }
		const ENexusMcpSessionState S = It.Value()->GetState();
		if (S == ENexusMcpSessionState::Closed) { It.RemoveCurrent(); ++Removed; continue; }
		// 非 Running 状态（握手未完成）超过 5 分钟视为孤儿
		if (S != ENexusMcpSessionState::Running)
		{
			const FTimespan Age = Now - It.Value()->GetCreatedAt();
			if (Age.GetTotalMinutes() > 5.0) { It.RemoveCurrent(); ++Removed; continue; }
		}
		// Running 状态超过 30 分钟无活动视为孤儿
		else
		{
			const FTimespan Idle = Now - It.Value()->GetLastActivityAt();
			if (Idle.GetTotalMinutes() > 30.0) { It.RemoveCurrent(); ++Removed; continue; }
		}
	}
	if (Removed > 0)
	{
		UE_LOG(LogNexusMcpServer, Log, TEXT("定期清理：移除 %d 个过期会话，剩余 %d 个"), Removed, HttpSessions.Num());
	}
	return true;
}

void FNexusMcpServer::BroadcastNotification(const FString& Method)
{
	// 构造 JSON-RPC 2.0 通知（无 id）
	TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Msg->SetStringField(TEXT("method"), Method);

	const FString JsonStr = FNexusJsonUtils::SerializeCondensed(Msg);

	// 向所有已连接的 WebSocket 客户端推送
	FTCHARToUTF8 Utf8Converter(*JsonStr);
	for (INetworkingWebSocket* Client : ConnectedClients)
	{
		if (Client && AuthenticatedWsClients.Contains(Client))
		{
			Client->Send(
				reinterpret_cast<const uint8*>(Utf8Converter.Get()),
				Utf8Converter.Length(),
				/*bPrependSize=*/false
			);
		}
	}

	UE_LOG(LogNexusMcpServer, Log, TEXT("已广播通知 '%s' 到 %d 个 WebSocket 客户端"),
		*Method, ConnectedClients.Num());
}
