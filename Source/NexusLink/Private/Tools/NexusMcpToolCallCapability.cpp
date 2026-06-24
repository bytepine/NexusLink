// Copyright byteyang. All Rights Reserved.

#include "Tools/NexusMcpToolCallCapability.h"
#include "NexusFeedback.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpToolRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/CriticalSection.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusCapabilityIndexUtils.h"
#include "Utils/NexusCapabilityLegacyNames.h"

// ── 进程内 redundant_call LRU 表 ──────────────────────────────────────────────
struct FCallCapabilityRedundantEntry
{
	FDateTime Ts;
	bool bHadAll = false;
};
static FCriticalSection GCallCapabilityRedundantMutex;
static TMap<FString, FCallCapabilityRedundantEntry> GCallCapabilityRedundantMap;

	/** 从 cap 的 Inner arguments 中提取首个 identity 字段值（用于 redundant_call key）。 */
	static FString ExtractIdentityKey(const TSharedPtr<FJsonObject>& Inner)
	{
		for (const TCHAR* Key : { TEXT("assetPath"), TEXT("actorName"), TEXT("widgetName"), TEXT("assetPaths") })
		{
			FString Val;
			if (Inner->TryGetStringField(Key, Val) && !Val.IsEmpty())
				return Val;
			// assetPaths 可能是数组，取首元
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Inner->TryGetArrayField(Key, Arr) && Arr && Arr->Num() > 0 && (*Arr)[0].IsValid())
			{
				if ((*Arr)[0]->TryGetString(Val) && !Val.IsEmpty())
					return Val;
			}
		}
		return FString();
	}

	/** 是否为 MCP 元工具名（不能经 call_capability 调用）。 */
	static bool IsMetaMcpToolName(const FString& CapName)
	{
		return CapName == TEXT("search_capabilities")
			|| CapName == TEXT("call_capability")
			|| CapName == TEXT("submit_feedback");
	}

	/** capability 未找到时的错误文案（含元工具误用、旧名提示）。 */
	static FString FormatUnknownCapabilityError(const FString& CapName)
	{
		if (IsMetaMcpToolName(CapName))
		{
			return FString::Printf(
				TEXT("'%s' 是 MCP 元工具，请直接通过 MCP tools/call 调用，不要放入 call_capability(capability=...)."),
				*CapName);
		}
		const FString Canon = FNexusCapabilityLegacyNames::GetCanonicalNameForLegacy(CapName);
		if (!Canon.IsEmpty())
		{
			return FString::Printf(
				TEXT("未知 Capability '%s'（旧名，当前规范名为 '%s'）。请使用规范名或 search_capabilities。"),
				*CapName, *Canon);
		}
		return FString::Printf(
			TEXT("未知 Capability '%s'，请通过 search_capabilities MCP 工具查询可用列表。"),
			*CapName);
	}

	static TSharedPtr<FJsonObject> BuildCallErrorObject(const FString& ErrorKind, const FString& Error,
	                                                    const FString& CapabilityName,
	                                                    const FString& Hint = FString(),
	                                                    const FString& RequestedName = FString())
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("errorKind"), ErrorKind);
		Err->SetStringField(TEXT("error"), Error);
		if (!CapabilityName.IsEmpty())
		{
			Err->SetStringField(TEXT("capability"), CapabilityName);
		}
		if (!Hint.IsEmpty())
		{
			Err->SetStringField(TEXT("hint"), Hint);
		}
		if (!RequestedName.IsEmpty() && !RequestedName.Equals(CapabilityName, ESearchCase::IgnoreCase))
		{
			Err->SetStringField(TEXT("requestedCapability"), RequestedName);
		}
		return Err;
	}

	/** 将 InputSchema 压成 parameters[]（含嵌套数组项），供 arg_invalid 响应复用。 */
	static void AppendParametersSchema(const TSharedPtr<FJsonObject>& InputSchema, const TSharedPtr<FJsonObject>& ErrObj)
	{
		if (!InputSchema.IsValid() || !ErrObj.IsValid())
		{
			return;
		}
		ErrObj->SetArrayField(TEXT("parameters"), FNexusCapabilityIndexUtils::ExtractParameters(InputSchema));
	}

	enum class ECallCoreStatus : uint8
	{
		Ok,
		RedundantWarn,
		Unknown,
		Disabled,
		ArgInvalid,
		Fatal
	};

	struct FCallCoreResult
	{
		ECallCoreStatus           Status = ECallCoreStatus::Ok;
		FString                   RequestedCapName;
		FString                   CapName;
		const FCapRecord*         Record = nullptr;
		TSharedPtr<FJsonObject>   TopOrWarn;
		TSharedPtr<FJsonObject>   ArgInvalidErr;
		FString                   FatalMessage;
	};

	/**
	 * 单条 capability 执行核心：查找、启用检查、redundant LRU、Run。
	 * 成功时 TopOrWarn 为 capability 顶层 JSON；redundant 时为 warning 对象。
	 */
	static FCallCoreResult RunCapabilityCore(const FString& CapName, const TSharedPtr<FJsonObject>& Inner)
	{
		FCallCoreResult R;
		R.RequestedCapName = CapName.TrimStartAndEnd();
		R.CapName          = FNexusCapabilityLegacyNames::Resolve(R.RequestedCapName);

		const FCapRecord* Record = FNexusCapabilityRegistry::Get().FindRecordByName(R.CapName);
		if (!Record)
		{
			R.Status = ECallCoreStatus::Unknown;
			{
				FNexusFeedback::FFields F;
				F.Tool       = TEXT("call_capability");
				F.Capability = R.RequestedCapName;
				F.ErrorText  = FormatUnknownCapabilityError(R.RequestedCapName);
				FNexusFeedback::RecordAuto(TEXT("call_unknown"), F);
			}
			return R;
		}
		R.Record = Record;

		const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
		if (!Settings->IsCapabilityEnabled(Record->Def.Name))
		{
			R.Status = ECallCoreStatus::Disabled;
			{
				FNexusFeedback::FFields F;
				F.Tool       = TEXT("call_capability");
				F.Capability = Record->Def.Name;
				F.ErrorText  = TEXT("已在设置中禁用");
				FNexusFeedback::RecordAuto(TEXT("call_disabled"), F);
			}
			return R;
		}

		// redundant_call 检测
		{
			const UNexusLinkSettings* Settings2 = UNexusLinkSettings::Get();
			const int32 WindowSec = Settings2 ? Settings2->RedundantCallWindowSec : 30;
			if (WindowSec > 0)
			{
				const FString Identity   = ExtractIdentityKey(Inner);
				const FString LruKey     = Record->Def.Name + TEXT("|") + Identity;
				const bool bIsSubSection = FNexusJsonUtils::HasSubSection(Inner);
				const bool bIsAll        = FNexusJsonUtils::HasSectionAll(Inner);
				FScopeLock Lock(&GCallCapabilityRedundantMutex);
				if (FCallCapabilityRedundantEntry* Entry = GCallCapabilityRedundantMap.Find(LruKey))
				{
					const double AgeSec = (FDateTime::UtcNow() - Entry->Ts).GetTotalSeconds();
					if (AgeSec <= WindowSec && Entry->bHadAll && bIsSubSection)
					{
						FNexusFeedback::FFields F;
						F.Tool       = TEXT("call_capability");
						F.Capability = Record->Def.Name;
						F.Note       = FString::Printf(TEXT("Sub-section call within %.0fs of sections=[\"all\"] for identity '%s'"), AgeSec, *Identity);
						FNexusFeedback::RecordAuto(TEXT("redundant_call"), F);

						TSharedPtr<FJsonObject> Warn = MakeShared<FJsonObject>();
						Warn->SetStringField(TEXT("warning"), TEXT("redundant_call"));
						Warn->SetStringField(TEXT("hint"), FString::Printf(
							TEXT("已跳过：%.0f 秒前已对 '%s' 调用 sections=[\"all\"]。请复用该响应，勿再调子 section。"),
							AgeSec, *Identity));
						Warn->SetBoolField(TEXT("redundant"), true);
						R.Status      = ECallCoreStatus::RedundantWarn;
						R.TopOrWarn   = Warn;
						return R;
					}
				}
				if (bIsAll || bIsSubSection)
				{
					FCallCapabilityRedundantEntry& E = GCallCapabilityRedundantMap.FindOrAdd(LruKey);
					E.Ts      = FDateTime::UtcNow();
					E.bHadAll = bIsAll;
				}
			}
		}

		FCapabilityResult CapResult = Record->Instance->Run(Inner);

		if (!CapResult.FatalError.IsEmpty())
		{
			FString Digest;
			for (const auto& Pair : Inner->Values)
			{
				if (!Digest.IsEmpty()) Digest += TEXT(",");
				Digest += Pair.Key;
			}

			if (CapResult.bIsArgInvalid)
			{
				FNexusFeedback::FFields F;
				F.Tool       = TEXT("call_capability");
				F.Capability = Record->Def.Name;
				F.ArgsDigest = Digest;
				F.ErrorText  = CapResult.FatalError;
				FNexusFeedback::RecordAuto(TEXT("call_arg_invalid"), F);

				TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
				ErrObj->SetStringField(TEXT("error"),      CapResult.FatalError);
				ErrObj->SetStringField(TEXT("capability"), Record->Def.Name);
				AppendParametersSchema(Record->Def.InputSchema, ErrObj);
				R.Status          = ECallCoreStatus::ArgInvalid;
				R.ArgInvalidErr   = ErrObj;
				return R;
			}

			FNexusFeedback::FFields F;
			F.Tool       = TEXT("call_capability");
			F.Capability = Record->Def.Name;
			F.ArgsDigest = Digest;
			F.ErrorText  = CapResult.FatalError;
			FNexusFeedback::RecordAuto(TEXT("call_fatal"), F);
			R.Status        = ECallCoreStatus::Fatal;
			R.FatalMessage  = CapResult.FatalError;
			return R;
		}

		TSharedPtr<FJsonObject> Top = CapResult.TopFields.IsValid()
			? CapResult.TopFields
			: MakeShared<FJsonObject>();
		if (CapResult.Entries.Num() > 0)
		{
			Top->SetArrayField(TEXT("results"), CapResult.Entries);
		}
		R.Status    = ECallCoreStatus::Ok;
		R.TopOrWarn = Top;
		return R;
	}

	static TSharedPtr<FJsonObject> MergeNestedArguments(const TSharedPtr<FJsonObject>& CallObj)
	{
		TSharedPtr<FJsonObject> Inner = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* Nested = nullptr;
		if (CallObj.IsValid() && CallObj->TryGetObjectField(TEXT("arguments"), Nested) && Nested && (*Nested).IsValid())
		{
			for (const auto& Pair : (*Nested)->Values)
			{
				Inner->SetField(Pair.Key, Pair.Value);
			}
		}
		return Inner;
	}

	static void SerializeObjToString(const TSharedPtr<FJsonObject>& Obj, FString& OutStr)
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	}
void FNexusMcpToolCallCapability::BuildDefinition(FNexusMcpToolDefinition& Out) const
{
	Out.Name = TEXT("call_capability");
	Out.Description = TEXT("【阶段4 - 执行操作】通过 capability 执行读写或交互。\n触发条件：search_capabilities 返回参数 Schema 后调用。\n前置依赖：必须先 search_capabilities 获取参数格式。\n用法：单条 capability+arguments；批量 calls=[{capability,arguments},...]。\n约束：失败看 errorKind (unknown/disabled/arg_invalid)；disabled 勿重试；_feedbackHint 必须 submit_feedback。");

	const TSharedPtr<FJsonObject> CallItemSchema = FNexusSchema::Object()
		.Prop(TEXT("capability"),
		       FNexusSchema::Str(TEXT("Capability 精确名称")))
		.Prop(TEXT("arguments"),
		      FNexusSchema::AnyObject(TEXT("本条的嵌套参数对象")))
		.Required({ TEXT("capability") })
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("capability"),
		       FNexusSchema::Str(TEXT("单次调用的 Capability 名称")))
		.Prop(TEXT("arguments"),
		      FNexusSchema::AnyObject(TEXT("单次调用的嵌套参数")))
		.Prop(TEXT("calls"),
		      FNexusSchema::ArrayOf(TEXT("批量：有序列表 [{capability,arguments?},...]"), CallItemSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FNexusMcpToolResult FNexusMcpToolCallCapability::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();

	const TArray<TSharedPtr<FJsonValue>>* CallsArr = nullptr;
	const bool bHasCallsKey = Args->TryGetArrayField(TEXT("calls"), CallsArr);

	if (bHasCallsKey && CallsArr)
	{
		if (CallsArr->Num() == 0)
		{
			Result.bIsError  = true;
			Result.ErrorText = TEXT("calls 数组至少包含一项");
			return Result;
		}

		FString SingleCap;
		if (Args->TryGetStringField(TEXT("capability"), SingleCap) && !SingleCap.IsEmpty())
		{
			Result.bIsError  = true;
			Result.ErrorText = TEXT("不可同时传 capability 与 calls；请二选一");
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> BatchResults;
		int32 SuccessCount = 0;
		int32 FailureCount = 0;

		for (const TSharedPtr<FJsonValue>& V : *CallsArr)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* CallObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(CallObj) || !CallObj || !(*CallObj).IsValid())
			{
				Item->SetStringField(TEXT("capability"), TEXT(""));
				Item->SetStringField(TEXT("error"), TEXT("calls 项必须是对象"));
				Item->SetStringField(TEXT("errorKind"), TEXT("invalid_item"));
				BatchResults.Add(MakeShared<FJsonValueObject>(Item));
				++FailureCount;
				continue;
			}

			FString CapName;
			if (!(*CallObj)->TryGetStringField(TEXT("capability"), CapName) || CapName.IsEmpty())
			{
				Item->SetStringField(TEXT("capability"), TEXT(""));
				Item->SetStringField(TEXT("error"), TEXT("calls 项缺少或为空 capability"));
				Item->SetStringField(TEXT("errorKind"), TEXT("arg_invalid"));
				BatchResults.Add(MakeShared<FJsonValueObject>(Item));
				++FailureCount;
				continue;
			}

			Item->SetStringField(TEXT("capability"), CapName);
			const TSharedPtr<FJsonObject> Inner = MergeNestedArguments(*CallObj);

			const FCallCoreResult Core = RunCapabilityCore(CapName, Inner);

			switch (Core.Status)
			{
			case ECallCoreStatus::Ok:
				Item->SetObjectField(TEXT("data"), Core.TopOrWarn);
				++SuccessCount;
				break;
			case ECallCoreStatus::RedundantWarn:
				Item->SetObjectField(TEXT("data"), Core.TopOrWarn);
				++SuccessCount;
				break;
	case ECallCoreStatus::Unknown:
		Item->SetStringField(TEXT("error"), FormatUnknownCapabilityError(Core.RequestedCapName));
		Item->SetStringField(TEXT("errorKind"), TEXT("unknown"));
		if (!Core.RequestedCapName.Equals(Core.CapName, ESearchCase::IgnoreCase))
		{
			Item->SetStringField(TEXT("requestedCapability"), Core.RequestedCapName);
		}
		Item->SetStringField(TEXT("_feedbackHint"), TEXT("建议 submit_feedback(category=\"wrong_tool\") 上报"));
		++FailureCount;
		break;
			case ECallCoreStatus::Disabled:
				Item->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Capability '%s' 已在设置中禁用。"), *Core.Record->Def.Name));
				Item->SetStringField(TEXT("errorKind"), TEXT("disabled"));
				Item->SetStringField(TEXT("hint"),
					TEXT("勿重试同名 cap。请在编辑器 编辑→项目设置→NexusLink 启用该 Capability，或改用只读方案。"));
				if (!Core.RequestedCapName.Equals(Core.Record->Def.Name, ESearchCase::IgnoreCase))
				{
					Item->SetStringField(TEXT("requestedCapability"), Core.RequestedCapName);
				}
				++FailureCount;
				break;
		case ECallCoreStatus::ArgInvalid:
			{
				FString ErrMsg;
				Core.ArgInvalidErr->TryGetStringField(TEXT("error"), ErrMsg);
				Item->SetStringField(TEXT("error"), ErrMsg);
				Item->SetStringField(TEXT("errorKind"), TEXT("arg_invalid"));
				Item->SetStringField(TEXT("_feedbackHint"), TEXT("建议 submit_feedback(category=\"schema_guess\") 上报参数歧义"));
				const TArray<TSharedPtr<FJsonValue>>* Par = nullptr;
				if (Core.ArgInvalidErr->TryGetArrayField(TEXT("parameters"), Par) && Par)
				{
					Item->SetArrayField(TEXT("parameters"), *Par);
				}
			}
			++FailureCount;
			break;
			case ECallCoreStatus::Fatal:
				Item->SetStringField(TEXT("error"), Core.FatalMessage);
				Item->SetStringField(TEXT("errorKind"), TEXT("fatal"));
				Item->SetStringField(TEXT("_feedbackHint"), TEXT("建议 submit_feedback(category=\"wrong_tool\") 上报此错误"));
				++FailureCount;
				break;
			}

			BatchResults.Add(MakeShared<FJsonValueObject>(Item));
		}

		TSharedPtr<FJsonObject> Top = MakeShared<FJsonObject>();
		Top->SetArrayField(TEXT("results"), BatchResults);
		Top->SetNumberField(TEXT("successCount"), SuccessCount);
		Top->SetNumberField(TEXT("failureCount"), FailureCount);
		Result.StructuredContent = Top;
		SerializeObjToString(Top, Result.OutputText);
		Result.bIsError = (FailureCount == BatchResults.Num() && BatchResults.Num() > 0);
		return Result;
	}

	// ── 单条形态（与历史响应兼容）────────────────────────────────────────────
	FString CapName;
	if (!Args->TryGetStringField(TEXT("capability"), CapName) || CapName.IsEmpty())
	{
		Result.bIsError  = true;
		Result.ErrorText = TEXT("缺少或为空 capability（批量请传 calls[]）");
		return Result;
	}

	const TSharedPtr<FJsonObject> Inner = MergeNestedArguments(Args);
	const FCallCoreResult Core = RunCapabilityCore(CapName, Inner);

	switch (Core.Status)
	{
	case ECallCoreStatus::Unknown:
		{
			Result.bIsError = true;
			TSharedPtr<FJsonObject> Err = BuildCallErrorObject(
				TEXT("unknown"),
				FormatUnknownCapabilityError(Core.RequestedCapName),
				FString(),
				TEXT("请 search_capabilities 查询规范名；勿重复 call_capability。"),
				Core.RequestedCapName);
			Err->SetStringField(TEXT("_feedbackHint"),
				TEXT("建议 submit_feedback(category=\"wrong_tool\") 上报"));
			Result.StructuredContent = Err;
			SerializeObjToString(Err, Result.ErrorText);
		}
		return Result;
	case ECallCoreStatus::Disabled:
		{
			Result.bIsError = true;
			const FString ErrMsg = FString::Printf(
				TEXT("Capability '%s' 已在设置中禁用。"), *Core.Record->Def.Name);
			TSharedPtr<FJsonObject> Err = BuildCallErrorObject(
				TEXT("disabled"),
				ErrMsg,
				Core.Record->Def.Name,
				TEXT("勿重试同名 cap。请在编辑器 编辑→项目设置→NexusLink 启用该 Capability，或改用只读方案。"),
				Core.RequestedCapName);
			Result.StructuredContent = Err;
			SerializeObjToString(Err, Result.ErrorText);
		}
		return Result;
	case ECallCoreStatus::RedundantWarn:
		Result.StructuredContent = Core.TopOrWarn;
		SerializeObjToString(Core.TopOrWarn, Result.OutputText);
		return Result;
	case ECallCoreStatus::ArgInvalid:
		{
			Result.bIsError = true;
			Core.ArgInvalidErr->SetStringField(TEXT("_feedbackHint"),
				TEXT("建议 submit_feedback(category=\"schema_guess\") 上报参数歧义"));
			FString ErrStr;
			SerializeObjToString(Core.ArgInvalidErr, ErrStr);
			Result.ErrorText = ErrStr;
		}
		return Result;
	case ECallCoreStatus::Fatal:
		Result.bIsError  = true;
		Result.ErrorText = Core.FatalMessage + TEXT("\n→ 建议调用 submit_feedback(category=\"wrong_tool\") 上报此问题");
		return Result;
	case ECallCoreStatus::Ok:
	default:
		break;
	}

	Result.StructuredContent = Core.TopOrWarn;
	SerializeObjToString(Core.TopOrWarn, Result.OutputText);
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolCallCapability)
