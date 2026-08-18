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
#include "HAL/CriticalSection.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityIndexUtils.h"
#include "Utils/NexusCapabilityLegacyNames.h"
#include "Utils/NexusCapResultAdapter.h"
#include "Utils/NexusPackageLedger.h"
#include "Utils/NexusHostUtils.h"
#include "Utils/NexusEditorTransaction.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

// ── 进程内 redundant_call LRU 表 ──────────────────────────────────────────────
struct FNexusCallCapabilityRedundantEntry
{
	FDateTime Ts;
	bool bHadAll = false;
};
static FCriticalSection GCallCapabilityRedundantMutex;
static TMap<FString, FNexusCallCapabilityRedundantEntry> GCallCapabilityRedundantMap;

/** 丢掉超出 redundant 窗口的条目。调用方须已持 GCallCapabilityRedundantMutex。 */
static void EvictStaleRedundantEntries(const FDateTime& Now, double WindowSec)
{
	for (auto It = GCallCapabilityRedundantMap.CreateIterator(); It; ++It)
	{
		if ((Now - It.Value().Ts).GetTotalSeconds() > WindowSec)
		{
			It.RemoveCurrent();
		}
	}
}

// 脱敏参数快照统一走 FNexusFeedback::BuildRedactedArgsSnapshot（公开 API，避免重复实现）。

/** 从 cap 的 Inner arguments 中提取首个 identity 字段值（用于 redundant_call key）。 */
static FString ExtractIdentityKey(const TSharedPtr<FJsonObject>& Inner)
{
	// 仅认单数定位键；跨目标由 call_capability.calls[] 拆分，不再读 assetPaths 等批量键
	for (const TCHAR* Key : { TEXT("assetPath"), TEXT("actorName"), TEXT("widgetName") })
	{
		FString Val;
		if (Inner->TryGetStringField(Key, Val) && !Val.IsEmpty())
			return Val;
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
				TEXT("'%s' is an MCP meta-tool; call it directly via MCP tools/call, not via call_capability(capability=...)."),
				*CapName);
	}
	const FString Canon = FNexusCapabilityLegacyNames::GetCanonicalNameForLegacy(CapName);
	if (!Canon.IsEmpty())
	{
		return FString::Printf(
				TEXT("Unknown capability '%s' (legacy name; canonical name is '%s'). Use the canonical name or search_capabilities."),
				*CapName, *Canon);
	}
	return FString::Printf(
			TEXT("Unknown capability '%s'. Use the search_capabilities MCP tool to list available capabilities."),
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

struct FNexusCallCore final
{
	FNexusCallCore() = delete;

	enum class EStatus : uint8
	{
		Ok,
		RedundantWarn,
		Unknown,
		Disabled,
		Unavailable,
		ArgInvalid,
		Fatal
	};

	struct FResult
	{
		EStatus                   Status = EStatus::Ok;
		FString                   RequestedCapName;
		FString                   CapName;
		const FCapRecord*         Record = nullptr;
		TSharedPtr<FJsonObject>   TopOrWarn;
		TSharedPtr<FJsonObject>   ArgInvalidErr;
		FString                   FatalMessage;
	};
};

/**
 * 单条 capability 执行核心：查找、启用检查、redundant LRU、Run。
 * 成功时 TopOrWarn 为 capability 顶层 JSON；redundant 时为 warning 对象。
 */
static FNexusCallCore::FResult RunCapabilityCore(const FString& CapName, const TSharedPtr<FJsonObject>& Inner)
{
	FNexusCallCore::FResult R;
	R.RequestedCapName = CapName.TrimStartAndEnd();
	R.CapName          = FNexusCapabilityLegacyNames::Resolve(R.RequestedCapName);

	const FCapRecord* Record = FNexusCapabilityRegistry::Get().FindRecordByName(R.CapName);
	if (!Record)
	{
		R.Status = FNexusCallCore::EStatus::Unknown;
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

	if (!FNexusHostUtils::IsCapabilityVisibleOnHost(*Record))
	{
		R.Status = FNexusCallCore::EStatus::Unavailable;
		{
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("call_capability");
			F.Capability = Record->Def.Name;
			F.ErrorText  = TEXT("Unavailable on current host (runtime only)");
			FNexusFeedback::RecordAuto(TEXT("call_disabled"), F);
		}
		return R;
	}

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!Settings->IsCapabilityEnabled(Record->Def.Name))
	{
		R.Status = FNexusCallCore::EStatus::Disabled;
		{
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("call_capability");
			F.Capability = Record->Def.Name;
			F.ErrorText  = TEXT("Disabled in settings");
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
			const FDateTime Now = FDateTime::UtcNow();
			EvictStaleRedundantEntries(Now, static_cast<double>(WindowSec));
			if (FNexusCallCapabilityRedundantEntry* Entry = GCallCapabilityRedundantMap.Find(LruKey))
			{
				const double AgeSec = (Now - Entry->Ts).GetTotalSeconds();
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
							TEXT("Skipped: sections=[\"all\"] was called for '%s' %.0f s ago. Reuse that response; do not call sub-sections again."),
							*Identity, AgeSec));
					Warn->SetBoolField(TEXT("redundant"), true);
					R.Status      = FNexusCallCore::EStatus::RedundantWarn;
					R.TopOrWarn   = Warn;
					return R;
				}
			}
			if (bIsAll || bIsSubSection)
			{
				FNexusCallCapabilityRedundantEntry& E = GCallCapabilityRedundantMap.FindOrAdd(LruKey);
				E.Ts      = Now;
				E.bHadAll = bIsAll;
			}
		}
	}

	FCapabilityResult CapResult = Record->Instance->Run(Inner);

	if (!CapResult.FatalError.IsEmpty())
	{
		const FString Digest = FNexusFeedback::BuildRedactedArgsSnapshot(Inner);

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
			R.Status          = FNexusCallCore::EStatus::ArgInvalid;
			R.ArgInvalidErr   = ErrObj;
			return R;
		}

		FNexusFeedback::FFields F;
		F.Tool       = TEXT("call_capability");
		F.Capability = Record->Def.Name;
		F.ArgsDigest = Digest;
		F.ErrorText  = CapResult.FatalError;
		FNexusFeedback::RecordAuto(TEXT("call_fatal"), F);
		R.Status        = FNexusCallCore::EStatus::Fatal;
		R.FatalMessage  = CapResult.FatalError;
		return R;
	}

	R.Status    = FNexusCallCore::EStatus::Ok;
	R.TopOrWarn = FNexusCapResultAdapter::AssembleStructuredContent(CapResult);
	FNexusCapResultAdapter::StripRedundantPathEcho(R.TopOrWarn, Inner, Record->Def.Name);
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

void FNexusMcpToolCallCapability::BuildDefinition(FNexusMcpToolDefinition& Out) const
{
	Out.Name = TEXT("call_capability");
	Out.Description = TEXT("[Stage 4 - Execute] Run read/write/interact via capability.\nTrigger: after search_capabilities returns parameter schema.\nPrerequisite: must search_capabilities first for parameter format.\nUsage: single capability+arguments; batch calls=[{capability,arguments},...].\nConstraints: on failure check errorKind (unknown/disabled/unavailable/arg_invalid); do not retry disabled; _feedbackHint requires submit_feedback.");

	const TSharedPtr<FJsonObject> CallItemSchema = FNexusSchema::Object()
		.Prop(TEXT("capability"),
		       FNexusSchema::Str(TEXT("Exact capability name")))
		.Prop(TEXT("arguments"),
		      FNexusSchema::AnyObject(TEXT("Nested arguments object for this item")))
		.Required({ TEXT("capability") })
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("capability"),
		       FNexusSchema::Str(TEXT("Capability name for a single call")))
		.Prop(TEXT("arguments"),
		      FNexusSchema::AnyObject(TEXT("Nested arguments for a single call")))
		.Prop(TEXT("calls"),
		      FNexusSchema::ArrayOf(TEXT("Batch: ordered list [{capability,arguments?},...]"), CallItemSchema.ToSharedRef()))
		.Prop(TEXT("keepLoaded"),
		      FNexusSchema::Bool(TEXT("When true, do not auto-unload packages introduced by this call (single or batch); default false"), true, false))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FNexusMcpToolResult FNexusMcpToolCallCapability::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
	const FNexusArgs Parsed(Args);

	// 内存高水位批量驱逐：每次调用重置基线；keepLoaded=true 时本次调用（单条或整批 calls）整体不自动卸载。
	// RAII 保证无论从哪个分支 return，本次调用结束时都会尝试一次批尾强制 flush（未被抑制时）。
	FNexusPackageLedger::Get().ResetBaseline();
	const bool bKeepLoaded = Parsed.Bool(TEXT("keepLoaded"));
	FNexusPackageLedger::Get().SetSuppressedForThisCall(bKeepLoaded);
	struct FLedgerFlushGuard
	{
		~FLedgerFlushGuard() { FNexusPackageLedger::FlushRemainingUnlessSuppressed(); }
	} LedgerFlushGuard;

	const TArray<TSharedPtr<FJsonValue>>* CallsArr = nullptr;
	const bool bHasCallsKey = Args->TryGetArrayField(TEXT("calls"), CallsArr);

	if (bHasCallsKey && CallsArr)
	{
		if (CallsArr->Num() == 0)
		{
			Result.bIsError  = true;
			Result.ErrorText = TEXT("calls array must contain at least one item");
			return Result;
		}

		FString SingleCap = Parsed.Str(TEXT("capability"));
		if (!SingleCap.IsEmpty())
		{
			Result.bIsError  = true;
			Result.ErrorText = TEXT("Cannot pass both capability and calls; use one or the other");
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> BatchResults;
		int32 SuccessCount = 0;
		int32 FailureCount = 0;

#if WITH_EDITOR
		TUniquePtr<FScopedTransaction> BatchTx;
		if (CallsArr->Num() >= 2)
		{
			BatchTx = FNexusEditorTransaction::Begin(TEXT("call_capability.calls"));
		}
#endif

		for (const TSharedPtr<FJsonValue>& V : *CallsArr)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* CallObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(CallObj) || !CallObj || !(*CallObj).IsValid())
			{
				Item->SetStringField(TEXT("capability"), TEXT(""));
				Item->SetStringField(TEXT("error"), TEXT("Each calls item must be an object"));
				Item->SetStringField(TEXT("errorKind"), TEXT("invalid_item"));
				BatchResults.Add(MakeShared<FJsonValueObject>(Item));
				++FailureCount;
				continue;
			}

			const FString CapName = FNexusArgs(*CallObj).Str(TEXT("capability"));
			if (CapName.IsEmpty())
			{
				Item->SetStringField(TEXT("capability"), TEXT(""));
				Item->SetStringField(TEXT("error"), TEXT("calls item missing or empty capability"));
				Item->SetStringField(TEXT("errorKind"), TEXT("arg_invalid"));
				BatchResults.Add(MakeShared<FJsonValueObject>(Item));
				++FailureCount;
				continue;
			}

			Item->SetStringField(TEXT("capability"), CapName);
			const TSharedPtr<FJsonObject> Inner = MergeNestedArguments(*CallObj);

			const FNexusCallCore::FResult Core = RunCapabilityCore(CapName, Inner);

			switch (Core.Status)
			{
			case FNexusCallCore::EStatus::Ok:
				Item->SetObjectField(TEXT("data"), Core.TopOrWarn);
				++SuccessCount;
				break;
			case FNexusCallCore::EStatus::RedundantWarn:
				Item->SetObjectField(TEXT("data"), Core.TopOrWarn);
				++SuccessCount;
				break;
	case FNexusCallCore::EStatus::Unknown:
		Item->SetStringField(TEXT("error"), FormatUnknownCapabilityError(Core.RequestedCapName));
		Item->SetStringField(TEXT("errorKind"), TEXT("unknown"));
		if (!Core.RequestedCapName.Equals(Core.CapName, ESearchCase::IgnoreCase))
		{
			Item->SetStringField(TEXT("requestedCapability"), Core.RequestedCapName);
		}
		Item->SetStringField(TEXT("_feedbackHint"), TEXT("submit_feedback(category=\"wrong_tool\")"));
		++FailureCount;
		break;
			case FNexusCallCore::EStatus::Disabled:
				Item->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Capability '%s' is disabled in settings."), *Core.Record->Def.Name));
				Item->SetStringField(TEXT("errorKind"), TEXT("disabled"));
				Item->SetStringField(TEXT("hint"),
					TEXT("Do not retry the same cap. Enable it in Editor → Project Settings → NexusLink, or use a read-only alternative."));
				if (!Core.RequestedCapName.Equals(Core.Record->Def.Name, ESearchCase::IgnoreCase))
				{
					Item->SetStringField(TEXT("requestedCapability"), Core.RequestedCapName);
				}
				++FailureCount;
				break;
			case FNexusCallCore::EStatus::Unavailable:
				Item->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Capability '%s' is unavailable on the current host (Dedicated Server / Game exposes runtime capabilities only)."),
					*Core.Record->Def.Name));
				Item->SetStringField(TEXT("errorKind"), TEXT("unavailable"));
				Item->SetStringField(TEXT("hint"),
					TEXT("Do not retry. Use a runtime capability, or connect to a full Editor instance."));
				if (!Core.RequestedCapName.Equals(Core.Record->Def.Name, ESearchCase::IgnoreCase))
				{
					Item->SetStringField(TEXT("requestedCapability"), Core.RequestedCapName);
				}
				++FailureCount;
				break;
		case FNexusCallCore::EStatus::ArgInvalid:
			{
				FString ErrMsg;
				Core.ArgInvalidErr->TryGetStringField(TEXT("error"), ErrMsg);
				Item->SetStringField(TEXT("error"), ErrMsg);
				Item->SetStringField(TEXT("errorKind"), TEXT("arg_invalid"));
				Item->SetStringField(TEXT("_feedbackHint"), TEXT("submit_feedback(category=\"schema_guess\")"));
				const TArray<TSharedPtr<FJsonValue>>* Par = nullptr;
				if (Core.ArgInvalidErr->TryGetArrayField(TEXT("parameters"), Par) && Par)
				{
					Item->SetArrayField(TEXT("parameters"), *Par);
				}
			}
			++FailureCount;
			break;
			case FNexusCallCore::EStatus::Fatal:
				Item->SetStringField(TEXT("error"), Core.FatalMessage);
				Item->SetStringField(TEXT("errorKind"), TEXT("fatal"));
				Item->SetStringField(TEXT("_feedbackHint"), TEXT("submit_feedback(category=\"wrong_tool\")"));
				++FailureCount;
				break;
			}

			BatchResults.Add(MakeShared<FJsonValueObject>(Item));
		}

#if WITH_EDITOR
		bool bRevertedBatch = false;
		if (BatchTx.IsValid())
		{
			if (FailureCount > 0)
			{
				bRevertedBatch = BatchTx->IsOutstanding();
				FNexusEditorTransaction::CancelAndRevert(BatchTx);
			}
			BatchTx.Reset();
		}
#endif

		TSharedPtr<FJsonObject> Top = MakeShared<FJsonObject>();
		Top->SetArrayField(TEXT("results"), BatchResults);
		Top->SetNumberField(TEXT("successCount"), SuccessCount);
		Top->SetNumberField(TEXT("failureCount"), FailureCount);
		if (FailureCount > 0)
		{
			Top->SetStringField(TEXT("undoNote"),
#if WITH_EDITOR
				bRevertedBatch
					? TEXT("Batch memory edits rolled back (saveToDisk/compile stay outside the transaction).")
					: TEXT("Batch had failures; memory rollback skipped (no outstanding editor transaction).")
#else
				TEXT("Batch memory edits rolled back (saveToDisk/compile stay outside the transaction).")
#endif
			);
		}
		Result.StructuredContent = Top;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Top);
		Result.bIsError = (FailureCount == BatchResults.Num() && BatchResults.Num() > 0);
		return Result;
	}

	// ── 单条形态（与历史响应兼容）────────────────────────────────────────────
	const FString CapName = Parsed.Str(TEXT("capability"));
	if (CapName.IsEmpty())
	{
		Result.bIsError  = true;
		Result.ErrorText = TEXT("Missing or empty capability (use calls[] for batch)");
		return Result;
	}

	const TSharedPtr<FJsonObject> Inner = MergeNestedArguments(Args);
	const FNexusCallCore::FResult Core = RunCapabilityCore(CapName, Inner);

	switch (Core.Status)
	{
	case FNexusCallCore::EStatus::Unknown:
		{
			Result.bIsError = true;
			TSharedPtr<FJsonObject> Err = BuildCallErrorObject(
				TEXT("unknown"),
				FormatUnknownCapabilityError(Core.RequestedCapName),
				FString(),
				TEXT("Use search_capabilities to find the canonical name; do not repeat call_capability."),
				Core.RequestedCapName);
			Err->SetStringField(TEXT("_feedbackHint"),
				TEXT("submit_feedback(category=\"wrong_tool\")"));
			Result.StructuredContent = Err;
			Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Err);
		}
		return Result;
	case FNexusCallCore::EStatus::Disabled:
		{
			Result.bIsError = true;
			const FString ErrMsg = FString::Printf(
				TEXT("Capability '%s' is disabled in settings."), *Core.Record->Def.Name);
			TSharedPtr<FJsonObject> Err = BuildCallErrorObject(
				TEXT("disabled"),
				ErrMsg,
				Core.Record->Def.Name,
				TEXT("Do not retry the same cap. Enable it in Editor → Project Settings → NexusLink, or use a read-only alternative."),
				Core.RequestedCapName);
			Result.StructuredContent = Err;
			Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Err);
		}
		return Result;
	case FNexusCallCore::EStatus::Unavailable:
		{
			Result.bIsError = true;
			const FString ErrMsg = FString::Printf(
				TEXT("Capability '%s' is unavailable on the current host (Dedicated Server / Game exposes runtime capabilities only)."),
				*Core.Record->Def.Name);
			TSharedPtr<FJsonObject> Err = BuildCallErrorObject(
				TEXT("unavailable"),
				ErrMsg,
				Core.Record->Def.Name,
				TEXT("Do not retry. Use a runtime capability, or connect to a full Editor instance."),
				Core.RequestedCapName);
			Result.StructuredContent = Err;
			Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Err);
		}
		return Result;
	case FNexusCallCore::EStatus::RedundantWarn:
		Result.StructuredContent = Core.TopOrWarn;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Core.TopOrWarn);
		return Result;
	case FNexusCallCore::EStatus::ArgInvalid:
		{
			Result.bIsError = true;
			Core.ArgInvalidErr->SetStringField(TEXT("_feedbackHint"),
				TEXT("submit_feedback(category=\"schema_guess\")"));
			Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Core.ArgInvalidErr);
		}
		return Result;
	case FNexusCallCore::EStatus::Fatal:
		Result.bIsError  = true;
		Result.ErrorText = Core.FatalMessage + TEXT("\n→ submit_feedback(category=\"wrong_tool\")");
		return Result;
	case FNexusCallCore::EStatus::Ok:
	default:
		break;
	}

	Result.StructuredContent = Core.TopOrWarn;
	Result.OutputText = FNexusJsonUtils::SerializeCondensed(Core.TopOrWarn);
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolCallCapability)
