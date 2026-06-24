// Copyright byteyang. All Rights Reserved.

#include "Server/NexusMcpDispatcher.h"
#include "Utils/NexusVersionCompat.h"
#include "Server/NexusProxyConfig.h"
#include "NexusMcpToolRegistry.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "Utils/NexusCapResultAdapter.h"
#include "Utils/NexusResponseCompactorUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Dom/JsonValue.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusMcpDispatcher, Log, All);

// --- JSON 辅助函数（UE4/UE5 兼容） ---

/** 安全读取字符串字段，字段不存在时返回 false。 */
static bool NexusJsonGetString(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FString& OutValue)
{
	if (!Obj.IsValid() || !Obj->HasField(Key))
	{
		return false;
	}
	OutValue = Obj->GetStringField(Key);
	return true;
}

/** 安全读取嵌套对象字段，字段不存在时返回 nullptr。 */
static TSharedPtr<FJsonObject> NexusJsonGetObject(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	if (!Obj.IsValid() || !Obj->HasField(Key))
	{
		return nullptr;
	}
	return Obj->GetObjectField(Key);
}

static const FString SupportedProtocolVersion = TEXT("2025-06-18");
static const FString ServerName               = TEXT("Nexus-Unreal");
static const FString ServerVersion            = TEXT("0.0.0");

// JSON-RPC 2.0 标准错误码
static constexpr int32 JsonRpcParseError     = -32700;
static constexpr int32 JsonRpcInvalidRequest = -32600;
static constexpr int32 JsonRpcMethodNotFound = -32601;
static constexpr int32 JsonRpcInvalidParams  = -32602;
static constexpr int32 JsonRpcInternalError  = -32603;

FNexusMcpDispatcher::FNexusMcpDispatcher(FOnSendResponse InSendCallback)
	: SendCallback(MoveTemp(InSendCallback))
{
}

void FNexusMcpDispatcher::Dispatch(const FString& JsonLine, FOnSendResponse PerRequestSend)
{
	LastActivityAt = FDateTime::UtcNow();
	// 临时覆盖 SendCallback，确保响应发回本次请求的 OnComplete
	TGuardValue<FOnSendResponse> CallbackGuard(SendCallback, MoveTemp(PerRequestSend));
	TSharedPtr<FJsonObject> JsonMsg;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);

	if (!FJsonSerializer::Deserialize(Reader, JsonMsg) || !JsonMsg.IsValid())
	{
		UE_LOG(LogNexusMcpDispatcher, Warning, TEXT("JSON 解析失败: %s"), *JsonLine);
		SendError(nullptr, JsonRpcParseError, TEXT("JSON 解析错误"));
		return;
	}

	// 校验 jsonrpc 版本
	FString JsonRpcVersion;
	if (!NexusJsonGetString(JsonMsg, TEXT("jsonrpc"), JsonRpcVersion) || JsonRpcVersion != TEXT("2.0"))
	{
		SendError(nullptr, JsonRpcInvalidRequest, TEXT("无效的 JSON-RPC 版本"));
		return;
	}

	FString Method;
	if (!NexusJsonGetString(JsonMsg, TEXT("method"), Method))
	{
		SendError(nullptr, JsonRpcInvalidRequest, TEXT("缺少 method"));
		return;
	}

	// 请求携带 id，通知不携带
	TSharedPtr<FJsonValue> Id;
	if (JsonMsg->HasField(TEXT("id")))
	{
		Id = JsonMsg->TryGetField(TEXT("id"));
	}

	TSharedPtr<FJsonObject> Params = NexusJsonGetObject(JsonMsg, TEXT("params"));

	// 方法路由
	if (Method == TEXT("initialize"))
	{
		HandleInitialize(Id, Params);
	}
	else if (Method == TEXT("notifications/initialized"))
	{
		HandleInitialized();
	}
	else if (Method == TEXT("ping"))
	{
		// ping 是 MCP 协议内置方法，直接返回空结果
		SendResult(Id, MakeShared<FJsonObject>());
	}
	else if (Method == TEXT("tools/list"))
	{
		if (State != ENexusMcpSessionState::Running)
		{
			SendError(Id, JsonRpcInvalidRequest, TEXT("会话未初始化"));
			return;
		}
		HandleToolsList(Id, Params);
	}
	else if (Method == TEXT("tools/call"))
	{
		if (State != ENexusMcpSessionState::Running)
		{
			SendError(Id, JsonRpcInvalidRequest, TEXT("会话未初始化"));
			return;
		}
		HandleToolsCall(Id, Params);
	}
	else
	{
		UE_LOG(LogNexusMcpDispatcher, Warning, TEXT("未知方法: %s"), *Method);
		if (Id.IsValid())
		{
			SendError(Id, JsonRpcMethodNotFound, FString::Printf(TEXT("方法未找到: %s"), *Method));
		}
	}
}

void FNexusMcpDispatcher::SendResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (Id.IsValid())
	{
		Response->SetField(TEXT("id"), Id);
	}
	Response->SetObjectField(TEXT("result"), Result);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	SendCallback(OutputString);
}

void FNexusMcpDispatcher::SendError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (Id.IsValid())
	{
		Response->SetField(TEXT("id"), Id);
	}
	else
	{
		Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
	}

	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetNumberField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), Message);
	Response->SetObjectField(TEXT("error"), Error);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	SendCallback(OutputString);
}

/**
 * 读取 Plugin/Resources/InitializeInstructions[.MultiTool].md 并返回。
 * 按当前 ToolsListMode 选择对应文件：
 *   - SearchMode  → InitializeInstructions.md（完整路由表 + 调用规范）
 *   - MultiTool   → InitializeInstructions.MultiTool.md（精简全局约束）
 * 文件缺失时回退到最小占位符，保证握手不会失败。
 */
static FString BuildInitializeInstructions()
{
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	const bool bMultiTool = Settings &&
		Settings->ToolsListMode == ENexusToolsListMode::MultiTool;
	const TCHAR* FileName = bMultiTool
		? TEXT("InitializeInstructions.MultiTool.md")
		: TEXT("InitializeInstructions.SearchMode.md");

	FString Base;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink"));
	if (Plugin.IsValid())
	{
		const FString Path = FPaths::Combine(
			Plugin->GetBaseDir(), TEXT("Resources"), FileName);
		FFileHelper::LoadFileToString(Base, *Path);
	}
	if (Base.IsEmpty())
	{
		Base = TEXT("NexusLink MCP：Unreal 编辑器 + 运行时控制。");
	}
	return Base;
}

void FNexusMcpDispatcher::HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	// 客户端重连时会重新发 initialize，直接重置状态重新握手
	if (State != ENexusMcpSessionState::WaitingForInitialize)
	{
		UE_LOG(LogNexusMcpDispatcher, Log, TEXT("收到重复的 initialize 请求，重置会话状态"));
		State = ENexusMcpSessionState::WaitingForInitialize;
		ProtocolVersion.Empty();
	}

	FString RequestedVersion;
	NexusJsonGetString(Params, TEXT("protocolVersion"), RequestedVersion);

	// 服务端始终使用自身支持的版本
	ProtocolVersion = SupportedProtocolVersion;

	TSharedPtr<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
	ToolsCap->SetBoolField(TEXT("listChanged"), true);

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("tools"), ToolsCap);

	TSharedPtr<FJsonObject> ServerInfoObj = MakeShared<FJsonObject>();
	ServerInfoObj->SetStringField(TEXT("name"), ServerName);
	ServerInfoObj->SetStringField(TEXT("version"), ServerVersion);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfoObj);
	Result->SetStringField(TEXT("instructions"), BuildInitializeInstructions());

	State = ENexusMcpSessionState::WaitingForInitialized;
	SendResult(Id, Result);

	UE_LOG(LogNexusMcpDispatcher, Log, TEXT("initialize 握手完成（客户端请求: %s，使用: %s）"),
		*RequestedVersion, *ProtocolVersion);
}

void FNexusMcpDispatcher::HandleInitialized()
{
	if (State != ENexusMcpSessionState::WaitingForInitialized)
	{
		UE_LOG(LogNexusMcpDispatcher, Warning, TEXT("在非预期状态下收到 'initialized' 通知"));
		return;
	}
	State = ENexusMcpSessionState::Running;
	UE_LOG(LogNexusMcpDispatcher, Log, TEXT("会话初始化完成，已就绪"));
}

/** 将工具定义序列化为 MCP tools/list 格式的 JSON 对象。 */
static TSharedPtr<FJsonObject> ToolDefToJson(const FNexusMcpToolDefinition& Def)
{
	TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
	ToolObj->SetStringField(TEXT("name"), Def.Name);
	if (!Def.Description.IsEmpty())
	{
		ToolObj->SetStringField(TEXT("description"), Def.Description);
	}

	if (Def.InputSchema.IsValid())
	{
		ToolObj->SetObjectField(TEXT("inputSchema"), Def.InputSchema);
	}
	else
	{
		TSharedPtr<FJsonObject> EmptySchema = MakeShared<FJsonObject>();
		EmptySchema->SetStringField(TEXT("type"), TEXT("object"));
		ToolObj->SetObjectField(TEXT("inputSchema"), EmptySchema);
	}

	// MCP 规范 annotations：根据 Tags 映射 readOnlyHint
	TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
	if (Def.HasTag(FNexusMcpTags::Readonly))
	{
		Annotations->SetBoolField(TEXT("readOnlyHint"), true);
	}
	if (Def.HasTag(FNexusMcpTags::Write))
	{
		Annotations->SetBoolField(TEXT("readOnlyHint"), false);
	}
	if (Annotations->Values.Num() > 0)
	{
		ToolObj->SetObjectField(TEXT("annotations"), Annotations);
	}
	return ToolObj;
}

void FNexusMcpDispatcher::HandleToolsList(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();

	if (Settings->ToolsListMode == ENexusToolsListMode::MultiTool)
	{
		// MultiTool 模式：每个已启用 Capability 作为独立 Tool 暴露；保留 submit_feedback
		const TArray<FCapRecord>& AllRecords = FNexusCapabilityRegistry::Get().GetAllRecords();
		ToolsArray.Reserve(AllRecords.Num());
		for (const FCapRecord& Record : AllRecords)
		{
			if (!Settings->IsCapabilityEnabled(Record.Def.Name))
			{
				continue;
			}

			// MultiTool 模式下 AI 无法调用 search_capabilities 补充元信息，
			// 将 prerequisites / relatedCapabilities 拼入 description 末尾。
			FString Desc = Record.Def.Description;
			if (Record.Def.Prerequisites.Num() > 0)
			{
				Desc += TEXT(" [前置条件: ") + FString::Join(Record.Def.Prerequisites, TEXT(",")) + TEXT("]");
			}
			if (Record.Def.RelatedCapabilities.Num() > 0)
			{
				Desc += TEXT(" [see: ") + FString::Join(Record.Def.RelatedCapabilities, TEXT(",")) + TEXT("]");
			}

			FNexusMcpToolDefinition ToolDef;
			ToolDef.Name        = Record.Def.Name;
			ToolDef.Description = Desc;
			ToolDef.InputSchema = Record.Def.InputSchema;
			ToolDef.Tags        = Record.Def.Tags;
			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDefToJson(ToolDef)));
		}
		// 保留 submit_feedback（AI 反馈元工具不应因模式而消失）
		const TArray<FNexusMcpToolDefinition>& AllDefs = FNexusMcpToolRegistry::Get().GetAllDefinitions();
		for (const FNexusMcpToolDefinition& Def : AllDefs)
		{
			if (Def.Name == TEXT("submit_feedback"))
			{
				ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDefToJson(Def)));
				break;
			}
		}
	}
	else
	{
		// SearchMode（默认）：暴露 3 个元工具
		const TArray<FNexusMcpToolDefinition>& AllDefs = FNexusMcpToolRegistry::Get().GetAllDefinitions();
		ToolsArray.Reserve(AllDefs.Num());
		for (const FNexusMcpToolDefinition& Def : AllDefs)
		{
			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDefToJson(Def)));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), ToolsArray);
	SendResult(Id, Result);
}

void FNexusMcpDispatcher::EmitToolResult(const TSharedPtr<FJsonValue>& Id, const FNexusMcpToolResult& ToolResult)
{
	// 统一序列化
	FString ResponseText;
	if (ToolResult.bIsError)
	{
		ResponseText = ToolResult.ErrorText;
	}
	else if (!ToolResult.OutputText.IsEmpty())
	{
		ResponseText = ToolResult.OutputText;
	}
	else if (ToolResult.StructuredContent.IsValid())
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResponseText);
		FJsonSerializer::Serialize(ToolResult.StructuredContent.ToSharedRef(), W);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("isError"), ToolResult.bIsError);

	if (ToolResult.bIsError)
	{
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ToolResult.ErrorText);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetArrayField(TEXT("content"), ContentArray);
	}
	else if (ToolResult.StructuredContent.IsValid() && UNexusLinkSettings::Get()->ContentMode == ENexusContentMode::StructuredContent)
	{
		Result->SetObjectField(TEXT("structuredContent"), ToolResult.StructuredContent);
	}
	else if (!ToolResult.OutputText.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ToolResult.OutputText);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetArrayField(TEXT("content"), ContentArray);
	}
	else if (ToolResult.StructuredContent.IsValid())
	{
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ResponseText);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetArrayField(TEXT("content"), ContentArray);
	}

	SendResult(Id, Result);
}

/**
 * 向成功响应注入 TTL 元数据：
 *   _snapshotAt  — 所有成功响应均注入，供 AI 判断数据新鲜度。
 *   _ttl_seconds — 仅易过期 Capability 注入，key 为 capability 名。
 * @param CapabilityName 实际 capability 名（非 MCP tool 名）。
 */
static void InjectTtlMetadata(FNexusMcpToolResult& ToolResult, const FString& CapabilityName)
{
	if (ToolResult.bIsError || !ToolResult.StructuredContent.IsValid())
	{
		return;
	}

	ToolResult.StructuredContent->SetStringField(TEXT("_snapshotAt"),
		FDateTime::UtcNow().ToIso8601());

	static const TMap<FString, int32> CapTtlMap = {
		{ TEXT("get_output_log"),                  30 },
		{ TEXT("list_runtime_actors"),             60 },
		{ TEXT("list_runtime_widgets"),            60 },
		{ TEXT("get_runtime_actor_property"),      60 },
		{ TEXT("get_runtime_actor_animation"),     60 },
		{ TEXT("get_runtime_actor_behavior_tree"), 60 },
	};
	if (const int32* Ttl = CapTtlMap.Find(CapabilityName))
	{
		ToolResult.StructuredContent->SetNumberField(TEXT("_ttl_seconds"), *Ttl);
	}
}

void FNexusMcpDispatcher::HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid())
	{
		SendError(Id, JsonRpcInvalidParams, TEXT("缺少 params"));
		return;
	}

	FString ToolName;
	if (!NexusJsonGetString(Params, TEXT("name"), ToolName))
	{
		SendError(Id, JsonRpcInvalidParams, TEXT("缺少工具名"));
		return;
	}

	TSharedPtr<FJsonObject> Arguments = NexusJsonGetObject(Params, TEXT("arguments"));
	if (!Arguments.IsValid())
	{
		Arguments = MakeShared<FJsonObject>();
	}

	// 先在 ToolRegistry 中查找（SearchMode 的元工具 + submit_feedback 始终可用）
	const bool bHasTool = FNexusMcpToolRegistry::Get().HasTool(ToolName);

	// MultiTool 模式下：ToolRegistry 未命中时 fallback 到 CapabilityRegistry
	if (!bHasTool)
	{
		const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
		if (Settings->ToolsListMode == ENexusToolsListMode::MultiTool)
		{
			const FCapRecord* Record = FNexusCapabilityRegistry::Get().FindRecordByName(ToolName);
			if (!Record)
			{
				SendError(Id, JsonRpcMethodNotFound, FString::Printf(TEXT("工具未找到: %s"), *ToolName));
				return;
			}
			if (!Settings->IsCapabilityEnabled(Record->Def.Name))
			{
				SendError(Id, JsonRpcMethodNotFound, FString::Printf(TEXT("Capability '%s' 已在设置中禁用。"), *Record->Def.Name));
				return;
			}

			// 直接执行 Capability
			const double ExecStartSec = FPlatformTime::Seconds();
			FCapabilityResult CapResult = Record->Instance->Run(Arguments);
			FNexusMcpToolResult ToolResult = NexusCapResultAdapter::Convert(CapResult, Record->Def.Name);
			const double ExecDurationMs = (FPlatformTime::Seconds() - ExecStartSec) * 1000.0;
			(void)ExecDurationMs; // 预留给未来慢调用检测

			// 响应压缩
			if (!ToolResult.bIsError && ToolResult.StructuredContent.IsValid())
			{
				FNexusResponseCompactorUtils::AutoCompactRecursive(ToolResult.StructuredContent);
			}

			// TTL 元数据注入（MultiTool 模式下 ToolName 即 capability 名，可直接 lookup）
			InjectTtlMetadata(ToolResult, ToolName);

			EmitToolResult(Id, ToolResult);
			return;
		}

		SendError(Id, JsonRpcMethodNotFound, FString::Printf(TEXT("工具未找到: %s"), *ToolName));
		return;
	}

	TSharedPtr<FNexusMcpTool> Tool = FNexusMcpToolRegistry::Get().CreateTool(ToolName);
	if (!Tool.IsValid())
	{
		SendError(Id, JsonRpcInternalError, TEXT("创建工具实例失败"));
		return;
	}

	const double ExecStartSec = FPlatformTime::Seconds();
	FNexusMcpToolResult ToolResult = Tool->Execute(Arguments);
	const double ExecDurationMs = (FPlatformTime::Seconds() - ExecStartSec) * 1000.0;
	// 全工具默认响应压缩：对 StructuredContent 递归抽取所有"对象数组"字段的主流值
	// 到同级 <field>_defaults，条目内等值字段随即省略。受 bCompactResponseDefaults 总开关控制；
	// 工具内已手动写入 <field>_defaults 的数组会自动跳过避免双写。
	if (!ToolResult.bIsError && ToolResult.StructuredContent.IsValid())
	{
		FNexusResponseCompactorUtils::AutoCompactRecursive(ToolResult.StructuredContent);
	}

	// P6 TTL 元数据：SearchMode 下 ToolName 是 MCP tool 名（"call_capability" 等），
	// 需从 Arguments.capability 取实际 capability 名；其余 tool（submit_feedback 等）
	// 不在 TTL 表中，lookup miss 无副作用。
	{
		FString EffectiveCapName = ToolName;
		if (ToolName == TEXT("call_capability"))
		{
			NexusJsonGetString(Arguments, TEXT("capability"), EffectiveCapName);
		}
		InjectTtlMetadata(ToolResult, EffectiveCapName);
	}

	// 统一序列化：成功走 StructuredContent（pretty JSON），失败走 ErrorText
	// 极少数工具会填 OutputText 覆盖默认文本
	FString ResponseText;
	if (ToolResult.bIsError)
	{
		ResponseText = ToolResult.ErrorText;
	}
	else if (!ToolResult.OutputText.IsEmpty())
	{
		ResponseText = ToolResult.OutputText;
	}
	else if (ToolResult.StructuredContent.IsValid())
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResponseText);
		FJsonSerializer::Serialize(ToolResult.StructuredContent.ToSharedRef(), W);
	}

	// 构建 tool result 响应：content 与 structuredContent 二选一。
	// ContentMode=Content（默认）：仅 content[0].text（完整 JSON），不输出 structuredContent；
	// ContentMode=StructuredContent：仅 structuredContent（原生 JSON），不输出 content。
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("isError"), ToolResult.bIsError);

	if (ToolResult.bIsError)
	{
		// 错误始终走 content
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ToolResult.ErrorText);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetArrayField(TEXT("content"), ContentArray);
	}
	else if (ToolResult.StructuredContent.IsValid() && UNexusLinkSettings::Get()->ContentMode == ENexusContentMode::StructuredContent)
	{
		// ContentMode=StructuredContent：优先走 structuredContent（即使工具设了 OutputText 也走结构化）
		Result->SetObjectField(TEXT("structuredContent"), ToolResult.StructuredContent);
	}
	else if (!ToolResult.OutputText.IsEmpty())
	{
		// 工具显式提供纯文本输出，走 content
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ToolResult.OutputText);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetArrayField(TEXT("content"), ContentArray);
	}
	else if (ToolResult.StructuredContent.IsValid())
	{
		// ContentMode=Content（默认）：StructuredContent 序列化为 content[0].text
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ResponseText);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetArrayField(TEXT("content"), ContentArray);
	}

	const int64 ResponseBytes = static_cast<int64>(ResponseText.Len());
	SendResult(Id, Result);
	UE_LOG(LogNexusMcpDispatcher, Log, TEXT("工具 '%s' 执行%s（%.0f ms, %lld 字节）"),
		*ToolName, ToolResult.bIsError ? TEXT("出错") : TEXT("成功"),
		ExecDurationMs, ResponseBytes);
}

void FNexusMcpDispatcher::DispatchDirect(const FString& JsonLine, FOnSendResponse PerRequestSend)
{
	TGuardValue<FOnSendResponse> CallbackGuard(SendCallback, MoveTemp(PerRequestSend));
	TSharedPtr<FJsonObject> JsonMsg;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);

	if (!FJsonSerializer::Deserialize(Reader, JsonMsg) || !JsonMsg.IsValid())
	{
		SendError(nullptr, JsonRpcParseError, TEXT("JSON 解析错误"));
		return;
	}

	FString Method;
	if (!NexusJsonGetString(JsonMsg, TEXT("method"), Method))
	{
		SendError(nullptr, JsonRpcInvalidRequest, TEXT("缺少 method"));
		return;
	}

	TSharedPtr<FJsonValue> Id;
	if (JsonMsg->HasField(TEXT("id")))
	{
		Id = JsonMsg->TryGetField(TEXT("id"));
	}

	TSharedPtr<FJsonObject> Params = NexusJsonGetObject(JsonMsg, TEXT("params"));

	// 无状态路由：直接处理，无需 MCP 握手
	if (Method == TEXT("ping"))
	{
		SendResult(Id, MakeShared<FJsonObject>());
	}
	else if (Method == TEXT("tools/list"))
	{
		HandleToolsList(Id, Params);
	}
	else if (Method == TEXT("tools/call"))
	{
		HandleToolsCall(Id, Params);
	}
	else if (Method == TEXT("status"))
	{
		// 返回编辑器状态信息（与 GET /status 相同内容，供 WebSocket 通道使用）
		TSharedPtr<FJsonObject> StatusObj = MakeShared<FJsonObject>();
		StatusObj->SetStringField(TEXT("server"), TEXT("Nexus-Unreal"));
		StatusObj->SetStringField(TEXT("version"), ServerVersion);
		StatusObj->SetStringField(TEXT("engineVersion"), FString::Printf(TEXT("%d.%d"),
			ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION));
		StatusObj->SetStringField(TEXT("projectName"), FApp::GetProjectName());
		SendResult(Id, StatusObj);
	}
	else if (Method == TEXT("nexus/instructions"))
	{
		// 返回 InitializeInstructions.md 内容，供 WS 通道的代理拼接到自身 initialize.instructions。
		// 不复用 MCP initialize：避免污染握手语义和状态机。
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("instructions"), BuildInitializeInstructions());
		SendResult(Id, Obj);
	}
	else if (Method == TEXT("nexus/proxy_config"))
	{
		// 返回 ProxyConfig.json 内容，供 IDE 代理动态获取连接工具描述与 AI 引导文案。
		SendResult(Id, FNexusProxyConfig::BuildConfigObject());
	}
	else
	{
		if (Id.IsValid())
		{
			SendError(Id, JsonRpcMethodNotFound, FString::Printf(TEXT("方法未找到: %s"), *Method));
		}
	}
}



