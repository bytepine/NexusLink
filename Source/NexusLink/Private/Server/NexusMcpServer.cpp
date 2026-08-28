// Copyright byteyang. All Rights Reserved.

#include "Server/NexusMcpServer.h"
#include "Server/NexusMcpDispatcher.h"
#include "NexusLinkSettings.h"
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
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "NexusMcpAuth.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusMcpServer, Log, All);

static const FString StreamEndpoint  = TEXT("/stream");
static const FString StatusEndpoint  = TEXT("/status");
static const FString McpSessionHeader = TEXT("Mcp-Session-Id");
static const int32 MaxMcpBodyBytes = 1024 * 1024;

/**
 * 把绑定地址写成本端口的 ListenerOverrides 条目。
 * 不再改 DefaultBindAddress——那是全局键，会连带影响工程内其他 HTTP 服务且我们从不回滚。
 */
static void ApplyHttpBindAddress(int32 Port, const TCHAR* BindAddr)
{
	if (!GConfig)
	{
		return;
	}

	static const FString Section = TEXT("HTTPServer.Listeners");

	FString LegacyDefaultBind;
	GConfig->GetString(*Section, TEXT("DefaultBindAddress"), LegacyDefaultBind, GEngineIni);
	if (!LegacyDefaultBind.IsEmpty())
	{
		UE_LOG(LogNexusMcpServer, Warning,
			TEXT("Engine.ini 里 HTTPServer.Listeners.DefaultBindAddress=%s 由旧版本 NexusLink 写入且影响全局；")
			TEXT("本插件已改为只写本端口 ListenerOverrides，该键可手动删除"), *LegacyDefaultBind);
	}

	TArray<FString> Overrides;
	GConfig->GetArray(*Section, TEXT("ListenerOverrides"), Overrides, GEngineIni);
	Overrides.RemoveAll([Port](const FString& Entry)
	{
		FString Cleaned = Entry;
		Cleaned.ReplaceInline(TEXT("("), TEXT(""));
		Cleaned.ReplaceInline(TEXT(")"), TEXT(""));
		uint32 ConfiguredPort = 0;
		return FParse::Value(*Cleaned, TEXT("Port="), ConfiguredPort)
			&& ConfiguredPort == static_cast<uint32>(Port);
	});
	Overrides.Add(FString::Printf(TEXT("(Port=%d,BindAddress=%s)"), Port, BindAddr));
	GConfig->SetArray(*Section, TEXT("ListenerOverrides"), Overrides, GEngineIni);

#if NX_UE_AT_LEAST(5, 2)
	// UE 5.8 起 FHttpServerConfig 缓存 listener 配置，只有该委托能让缓存失效；
	// 否则进程内第一次监听的地址会锁定整个会话，热切换局域网绑定不生效。
	FCoreDelegates::TSOnConfigSectionsChanged().Broadcast(GEngineIni, TSet<FString>{ Section });
#endif
}

static FString GetPluginVersionName()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink")))
	{
		return Plugin->GetDescriptor().VersionName;
	}
	return TEXT("0.0.0");
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
	AuthToken     = FNexusMcpAuth::LoadOrCreateMachineToken();

	const bool bLan = UNexusLinkSettings::Get() && UNexusLinkSettings::Get()->bAllowLanBind;
	const TCHAR* BindAddr = bLan ? TEXT("0.0.0.0") : TEXT("127.0.0.1");
	ApplyHttpBindAddress(McpPort, BindAddr);

	// 设置面板改这两项时会弹确认框，但直接改 ini 绕得过去，这里补一道兜底告警
	if (bLan && !UNexusLinkSettings::IsMcpAuthRequired())
	{
		UE_LOG(LogNexusMcpServer, Error,
			TEXT("当前为「局域网绑定 + 关闭鉴权」：同网段任意主机都能无凭证控制本编辑器。")
			TEXT("请开启 MCP 鉴权或取消局域网绑定，且不要把端口映射到公网"));
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
		TEXT("NexusLink 服务器已启动（bind %s）\n  Streamable HTTP : http://127.0.0.1:%d/stream\n  Status          : http://127.0.0.1:%d/status\n  WebSocket       : ws://127.0.0.1:%d/"),
		BindAddr, McpPort, McpPort, WebSocketPort);
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
			const bool bRequireAuth = UNexusLinkSettings::IsMcpAuthRequired();
			if (bRequireAuth && !ExtractBearerToken(GetRequestHeader(Request, TEXT("Authorization")), PresentedToken))
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

				if (UNexusLinkSettings::IsMcpAuthRequired()
					&& !FNexusMcpAuth::IsTokenAccepted(
						PresentedToken,
						Server->GetAuthToken(),
						UNexusLinkSettings::Get() ? UNexusLinkSettings::Get()->GetExtraMcpAuthTokensText() : FString()))
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
				Obj->SetStringField(TEXT("version"), GetPluginVersionName());
				Obj->SetStringField(TEXT("engineVersion"),
					FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION));
				Obj->SetStringField(TEXT("projectName"), FApp::GetProjectName());
				Obj->SetNumberField(TEXT("wsPort"), Server->GetWsPort());
				Obj->SetStringField(TEXT("netRole"), DetectCurrentNetRole());
				Obj->SetBoolField(TEXT("authRequired"), UNexusLinkSettings::IsMcpAuthRequired());
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
	const bool bLan = UNexusLinkSettings::Get() && UNexusLinkSettings::Get()->bAllowLanBind;

#if NX_UE_HAS_WS_BIND_ADDRESS
	const FString WsBindAddress = bLan ? TEXT("0.0.0.0") : TEXT("127.0.0.1");
	const bool bWsInited = WebSocketServer->Init(static_cast<uint32>(WsPort), OnConnected, WsBindAddress);
#else
	// 5.1 及更早的 Init 不暴露绑定地址，WS 一定绑全部网卡；仅靠首帧 auth 兜底
	const bool bWsInited = WebSocketServer->Init(static_cast<uint32>(WsPort), OnConnected);
#endif
	if (!bWsInited)
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

#if NX_UE_HAS_WS_BIND_ADDRESS
	UE_LOG(LogNexusMcpServer, Log, TEXT("WebSocket 服务器已启动，端口: %d，绑定: %s（须 WS 首帧 auth）"),
		WsPort, *WsBindAddress);
#else
	UE_LOG(LogNexusMcpServer, Log,
		TEXT("WebSocket 服务器已启动，端口: %d（UE 5.1 及更早的引擎 Init 不支持指定绑定地址，实际绑全部网卡；须 WS 首帧 auth）"),
		WsPort);
	if (!UNexusLinkSettings::IsMcpAuthRequired())
	{
		UE_LOG(LogNexusMcpServer, Error,
			TEXT("WebSocket 端口 %d 在本引擎版本必然对局域网可达，而 MCP 鉴权已关闭：")
			TEXT("同网段任意主机都能无凭证控制本编辑器，请开启 MCP 鉴权"), WsPort);
	}
#endif
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

	// 与 HTTP 通道同一上限，避免超大帧在 GameThread 上解析
	if (DataSize > MaxMcpBodyBytes)
	{
		UE_LOG(LogNexusMcpServer, Warning, TEXT("WebSocket 帧超过 1MB（%d 字节），已丢弃"), DataSize);
		SendWsText(ClientWebSocket, MakeJsonRpcError(nullptr, -32600, TEXT("payload_too_large")));
		return;
	}

	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), DataSize);
	FString JsonLine(Converter.Length(), Converter.Get());

	// 勿在 WebSocket 收包回调里同步执行 tools/call：search_asset 等会阻塞 GameThread 数秒～数十秒，
	// 同帧内无法继续 TickWebSocket，代理侧长连接易被判定超时/断开。推迟到本帧后续 GameThread 任务执行。
	// 与 HTTP 路径一致用弱引用：任务跨帧执行，期间服务器可能已被停掉/析构
	const TWeakPtr<FNexusMcpServer> WeakSelf = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, ClientWebSocket, JsonLine = MoveTemp(JsonLine)]()
	{
		const TSharedPtr<FNexusMcpServer> Self = WeakSelf.Pin();
		if (!Self.IsValid() || !Self->IsRunning())
		{
			return;
		}
		if (!Self->ConnectedClients.Contains(ClientWebSocket) || !Self->WsDispatcher.IsValid())
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

		const bool bRequireAuth = UNexusLinkSettings::IsMcpAuthRequired();
		if (Method == TEXT("auth"))
		{
			FString Presented;
			const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
			if (JsonMsg.IsValid() && JsonMsg->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj)
			{
				(*ParamsObj)->TryGetStringField(TEXT("token"), Presented);
			}
			// 鉴权关闭时仍接受 auth 并回 ok，兼容误发首帧的中转
			const FString Extra = UNexusLinkSettings::Get() ? UNexusLinkSettings::Get()->GetExtraMcpAuthTokensText() : FString();
			if (!bRequireAuth || FNexusMcpAuth::IsTokenAccepted(Presented, Self->AuthToken, Extra))
			{
				Self->AuthenticatedWsClients.Add(ClientWebSocket);
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

		if (bRequireAuth && !Self->AuthenticatedWsClients.Contains(ClientWebSocket) && Method != TEXT("ping"))
		{
			SendWsText(ClientWebSocket, MakeJsonRpcError(Id, -32001, TEXT("unauthorized")));
			return;
		}

		FString ResponseJson;
		Self->WsDispatcher->DispatchDirect(JsonLine, [&ResponseJson](const FString& Json)
		{
			ResponseJson = Json;
		});
		SendWsText(ClientWebSocket, ResponseJson);
	});
}

void FNexusMcpServer::ResetWsAuthentications()
{
	AuthenticatedWsClients.Empty();
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
