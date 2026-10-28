// Copyright byteyang. All Rights Reserved.

#include "Tools/NexusMcpToolCallCapability.h"
#include "NexusFeedback.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusMcpTool.h"
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
#include "Utils/NexusDangerousCapGate.h"

// ── 进程内 RedundantCall LRU 表 ──────────────────────────────────────────────
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

/** 从 cap 的 Inner arguments 中提取首个 identity 字段值（用于 RedundantCall key）。 */
static FString ExtractIdentityKey(const TSharedPtr<FJsonObject>& Inner)
{
	// 仅认单数定位键；跨目标由 CallCapability.calls[] 拆分，不再读 assetPaths 等批量键
	for (const TCHAR* Key : { TEXT("AssetPath"), TEXT("ActorName"), TEXT("WidgetName") })
	{
		FString Val;
		if (Inner->TryGetStringField(Key, Val) && !Val.IsEmpty())
			return Val;
	}
	return FString();
}

/** 是否为 MCP 元工具名（不能经 CallCapability 调用）。 */
static bool IsMetaMcpToolName(const FString& CapName)
{
	return CapName == TEXT("SearchCapabilities")
		|| CapName == TEXT("CallCapability")
		|| CapName == TEXT("SubmitFeedback");
}

/** capability 未找到时的错误文案（含元工具误用、旧名已解析但仍未注册）。 */
static FString FormatUnknownCapabilityError(const FString& RequestedName, const FString& ResolvedName)
{
	if (IsMetaMcpToolName(RequestedName))
	{
		return FString::Printf(
				TEXT("'%s' is an MCP meta-tool; call it directly via MCP tools/call, not via CallCapability(Capability=...)."),
				*RequestedName);
	}
	FString Msg;
	if (!RequestedName.Equals(ResolvedName, ESearchCase::IgnoreCase) && !ResolvedName.IsEmpty())
	{
		Msg = FString::Printf(
				TEXT("Unknown capability '%s' (resolved from legacy name to '%s', which is not registered). Use SearchCapabilities."),
				*RequestedName, *ResolvedName);
	}
	else
	{
		Msg = FString::Printf(
			TEXT("Unknown capability '%s'. Use the SearchCapabilities MCP tool to list available capabilities."),
			*RequestedName);
	}
	if (!FNexusHostUtils::IsFullEditorCapabilityHost())
	{
		Msg += TEXT(" Current host is Game/DS (runtime caps only); connect to an Editor instance (list_unreal_instances HostKind=Editor) for asset tools.");
	}
	return Msg;
}

static FString DisabledHint(const FCapRecord* Rec)
{
	if (Rec && Rec->Def.HasTag(FNexusMcpTags::Dangerous))
	{
		return TEXT("Do not retry the same cap. Set Editor Preferences → NexusLink → Dangerous Capability access to Confirm or Custom.");
	}
	return TEXT("Do not retry the same cap. Enable it in Editor Preferences → NexusLink → MCP Capabilities, or use a read-only alternative.");
}

static TSharedPtr<FJsonObject> BuildCallErrorObject(const FString& ErrorKind, const FString& Error,
	                                                    const FString& CapabilityName,
	                                                    const FString& Hint = FString(),
	                                                    const FString& RequestedName = FString())
{
	TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetStringField(TEXT("ErrorKind"), ErrorKind);
	Err->SetStringField(TEXT("Error"), Error);
	if (!CapabilityName.IsEmpty())
	{
		Err->SetStringField(TEXT("Capability"), CapabilityName);
	}
	if (!Hint.IsEmpty())
	{
		Err->SetStringField(TEXT("Hint"), Hint);
	}
	if (!RequestedName.IsEmpty() && !RequestedName.Equals(CapabilityName, ESearchCase::IgnoreCase))
	{
		Err->SetStringField(TEXT("RequestedCapability"), RequestedName);
	}
	return Err;
}

/** 将 InputSchema 压成 parameters[]（含嵌套数组项），供 ArgInvalid 响应复用。 */
static void AppendParametersSchema(const TSharedPtr<FJsonObject>& InputSchema, const TSharedPtr<FJsonObject>& ErrObj)
{
	if (!InputSchema.IsValid() || !ErrObj.IsValid())
	{
		return;
	}
	ErrObj->SetArrayField(TEXT("Parameters"), FNexusCapabilityIndexUtils::ExtractParameters(InputSchema));
}

struct FNexusCallCore final
{
	FNexusCallCore() = delete;

	enum class EStatus : uint8
	{
		Ok,
		Failed,
		Unknown,
		Disabled,
		Unavailable,
		ArgInvalid,
		UserDenied,
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
		FCapabilityResult         CapResult;
	};
};

/**
 * 单条 capability 执行核心：查找、启用检查、Run 与非阻断的重复读取提示。
 */
static FNexusCallCore::FResult RunCapabilityCore(const FString& CapName, const TSharedPtr<FJsonObject>& Inner, bool bFinalize = true)
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
			F.Tool       = TEXT("CallCapability");
			F.Capability = R.RequestedCapName;
			F.ErrorText  = FormatUnknownCapabilityError(R.RequestedCapName, R.CapName);
			FNexusFeedback::RecordAuto(TEXT("CallUnknown"), F);
		}
		return R;
	}
	R.Record = Record;

	if (!FNexusHostUtils::IsCapabilityVisibleOnHost(*Record))
	{
		R.Status = FNexusCallCore::EStatus::Unavailable;
		{
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("CallCapability");
			F.Capability = Record->Def.Name;
			F.ErrorText  = TEXT("Unavailable on current host (runtime only)");
			FNexusFeedback::RecordAuto(TEXT("CallDisabled"), F);
		}
		return R;
	}

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	if (!Settings->IsCapabilityEnabled(Record->Def.Name))
	{
		R.Status = FNexusCallCore::EStatus::Disabled;
		{
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("CallCapability");
			F.Capability = Record->Def.Name;
			F.ErrorText  = TEXT("Disabled in settings");
			FNexusFeedback::RecordAuto(TEXT("CallDisabled"), F);
		}
		return R;
	}

	R.CapResult = Record->Instance->Run(Inner, bFinalize);
	const FCapabilityResult& CapResult = R.CapResult;
	// 写入可能部分成功，报错也要使之前的读取提示失效。
	if (Record->Def.HasTag(FNexusMcpTags::Write))
	{
		FScopeLock Lock(&GCallCapabilityRedundantMutex);
		GCallCapabilityRedundantMap.Empty();
	}

	// 仅成功读取参与统计；提示不能替代当前数据，也不假定别的客户端持有旧响应。
	bool bRecentAllRead = false;
	if (!CapResult.HasErrors())
	{
		const UNexusLinkSettings* Settings2 = UNexusLinkSettings::Get();
		const int32 WindowSec = Settings2 ? Settings2->RedundantCallWindowSec : 30;
		const FString Identity = ExtractIdentityKey(Inner);
		if (WindowSec > 0 && !Identity.IsEmpty())
		{
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
					F.Tool       = TEXT("CallCapability");
					F.Capability = Record->Def.Name;
					F.Note       = FString::Printf(TEXT("Sub-section call within %.0fs of Sections=[\"all\"] for identity '%s'"), AgeSec, *Identity);
					FNexusFeedback::RecordAuto(TEXT("RedundantCall"), F);

					bRecentAllRead = true;
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

	if (!CapResult.FatalError.IsEmpty())
	{
		const FString Digest = FNexusFeedback::BuildRedactedArgsSnapshot(Inner);

		if (CapResult.bIsUserDenied)
		{
			TSharedPtr<FJsonObject> ErrObj = BuildCallErrorObject(
				TEXT("UserDenied"),
				CapResult.FatalError,
				Record->Def.Name,
				TEXT("Do not retry; user denied this request."),
				R.RequestedCapName);
			R.Status        = FNexusCallCore::EStatus::UserDenied;
			R.ArgInvalidErr = ErrObj;
			return R;
		}

		if (CapResult.bIsArgInvalid)
		{
			if (!CapResult.bSkipFeedback)
			{
				FNexusFeedback::FFields F;
				F.Tool       = TEXT("CallCapability");
				F.Capability = Record->Def.Name;
				F.ArgsDigest = Digest;
				F.ErrorText  = CapResult.FatalError;
				FNexusFeedback::RecordAuto(TEXT("CallArgInvalid"), F);
			}

			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetStringField(TEXT("Error"),      CapResult.FatalError);
			ErrObj->SetStringField(TEXT("ErrorKind"),  TEXT("ArgInvalid"));
			ErrObj->SetStringField(TEXT("Capability"), Record->Def.Name);
			AppendParametersSchema(Record->Def.InputSchema, ErrObj);
			if (CapResult.bSkipFeedback)
			{
				ErrObj->SetStringField(TEXT("Hint"),
					TEXT("Pass reason (purpose, expected effect, why no safer dedicated capability) and retry once."));
			}
			R.Status          = FNexusCallCore::EStatus::ArgInvalid;
			R.ArgInvalidErr   = ErrObj;
			return R;
		}

		FNexusFeedback::FFields F;
		F.Tool       = TEXT("CallCapability");
		F.Capability = Record->Def.Name;
		F.ArgsDigest = Digest;
		F.ErrorText  = CapResult.FatalError;
		FNexusFeedback::RecordAuto(TEXT("CallFatal"), F);
		R.Status        = FNexusCallCore::EStatus::Fatal;
		R.FatalMessage  = CapResult.FatalError;
		return R;
	}

	R.Status    = CapResult.HasErrors() ? FNexusCallCore::EStatus::Failed : FNexusCallCore::EStatus::Ok;
	R.TopOrWarn = FNexusCapResultAdapter::AssembleStructuredContent(CapResult);
	if (bRecentAllRead)
	{
		R.TopOrWarn->SetStringField(TEXT("Warning"), TEXT("RedundantCall"));
		R.TopOrWarn->SetStringField(TEXT("Hint"), TEXT("A recent All read was detected; fresh data is returned for this request."));
	}
	// 后续批量收尾直接更新同一份 Data，避免序列化前丢掉编译/保存结果。
	R.CapResult.TopFields = R.TopOrWarn;
	FNexusCapResultAdapter::StripRedundantPathEcho(R.TopOrWarn, Inner, Record->Def.Name);
	return R;
}

static TSharedPtr<FJsonObject> MergeNestedArguments(const TSharedPtr<FJsonObject>& CallObj)
{
	TSharedPtr<FJsonObject> Inner = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* Nested = nullptr;
	if (CallObj.IsValid() && CallObj->TryGetObjectField(TEXT("Arguments"), Nested) && Nested && (*Nested).IsValid())
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
	Out.Name = TEXT("CallCapability");
	Out.Description = TEXT("[Stage 4 - Execute] Run read/write/interact via capability.\nTrigger: after SearchCapabilities returns parameter schema.\nPrerequisite: must SearchCapabilities first for parameter format.\nUsage: single capability+arguments; batch Calls=[{capability,arguments},...].\nConstraints: on failure check ErrorKind (unknown/disabled/unavailable/ArgInvalid/UserDenied); do not retry disabled or UserDenied; Confirm-mode dangerous caps need reason; FeedbackHint requires SubmitFeedback.");

	const TSharedPtr<FJsonObject> CallItemSchema = FNexusSchema::Object()
		.Prop(TEXT("Capability"),
		       FNexusSchema::Str(TEXT("Exact capability name")))
		.Prop(TEXT("Arguments"),
		      FNexusSchema::AnyObject(TEXT("Nested arguments object for this item")))
		.Required({ TEXT("Capability") })
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Capability"),
		       FNexusSchema::Str(TEXT("Capability name for a single call")))
		.Prop(TEXT("Arguments"),
		      FNexusSchema::AnyObject(TEXT("Nested arguments for a single call")))
		.Prop(TEXT("Calls"),
		      FNexusSchema::ArrayOf(TEXT("Batch: ordered list [{capability,arguments?},...]"), CallItemSchema.ToSharedRef()))
		.Prop(TEXT("bKeepLoaded"),
		      FNexusSchema::Bool(TEXT("When true, do not auto-unload packages introduced by this call (single or batch); default false"), true, false))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FNexusMcpToolResult FNexusMcpToolCallCapability::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
	const FNexusArgs Parsed(Args);

	// 内存高水位批量驱逐：每次调用重置基线；bKeepLoaded=true 时本次调用（单条或整批 calls）整体不自动卸载。
	// RAII 保证无论从哪个分支 return，本次调用结束时都会尝试一次批尾强制 flush（未被抑制时）。
	FNexusDangerousCapGate::FBatchScope DangerousBatch;

	FNexusPackageLedger::Get().ResetBaseline();
	const bool bKeepLoaded = Parsed.Bool(TEXT("bKeepLoaded"));
	FNexusPackageLedger::Get().SetSuppressedForThisCall(bKeepLoaded);
	struct FLedgerFlushGuard
	{
		~FLedgerFlushGuard() { FNexusPackageLedger::FlushRemainingUnlessSuppressed(); }
	} LedgerFlushGuard;

	const TArray<TSharedPtr<FJsonValue>>* CallsArr = nullptr;
	const bool bHasCallsKey = Args->TryGetArrayField(TEXT("Calls"), CallsArr);

	if (bHasCallsKey && CallsArr)
	{
		if (CallsArr->Num() == 0)
		{
			Result.bIsError  = true;
			Result.ErrorText = TEXT("calls array must contain at least one item");
			return Result;
		}

		FString SingleCap = Parsed.Str(TEXT("Capability"));
		if (!SingleCap.IsEmpty())
		{
			Result.bIsError  = true;
			Result.ErrorText = TEXT("Cannot pass both capability and calls; use one or the other");
			return Result;
		}

		// 执行前检查整批，不能在发生不可撤销副作用之后才拒绝。
		if (CallsArr->Num() > 0)
		{
			for (const auto& Call : *CallsArr)
			{
				if (!Call.IsValid() || Call->Type != EJson::Object) continue;
				FString Name;
				Call->AsObject()->TryGetStringField(TEXT("Capability"), Name);
				const FCapRecord* Record = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
				if (Record && !Record->Def.bAllowBatch)
				{
					Result.bIsError = true;
					Result.ErrorText = FString::Printf(TEXT("Capability '%s' requires a single call; the batch was not executed."), *Name);
					return Result;
				}
			}
		}
		TArray<TSharedPtr<FJsonValue>> BatchResults;
		struct FPendingFinalize
		{
			FNexusCallCore::FResult Core;
			TSharedPtr<FJsonObject> Arguments;
			TSharedPtr<FJsonObject> Item;
		};
		TArray<FPendingFinalize> PendingFinalizes;
		int32 SuccessCount = 0;
		int32 FailureCount = 0;
		const bool bDeferFinalize = CallsArr->Num() >= 2;

		TUniquePtr<INexusTransactionHandle> BatchTx;
		if (CallsArr->Num() >= 2)
		{
			BatchTx = FNexusEditorTransaction::Begin(TEXT("CallCapability.calls"));
		}

		for (const TSharedPtr<FJsonValue>& V : *CallsArr)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* CallObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(CallObj) || !CallObj || !(*CallObj).IsValid())
			{
				Item->SetStringField(TEXT("Capability"), TEXT(""));
				Item->SetStringField(TEXT("Error"), TEXT("Each calls item must be an object"));
				Item->SetStringField(TEXT("ErrorKind"), TEXT("InvalidItem"));
				BatchResults.Add(MakeShared<FJsonValueObject>(Item));
				++FailureCount;
				continue;
			}

			const FString CapName = FNexusArgs(*CallObj).Str(TEXT("Capability"));
			if (CapName.IsEmpty())
			{
				Item->SetStringField(TEXT("Capability"), TEXT(""));
				Item->SetStringField(TEXT("Error"), TEXT("calls item missing or empty capability"));
				Item->SetStringField(TEXT("ErrorKind"), TEXT("ArgInvalid"));
				BatchResults.Add(MakeShared<FJsonValueObject>(Item));
				++FailureCount;
				continue;
			}

			Item->SetStringField(TEXT("Capability"), CapName);
			const TSharedPtr<FJsonObject> Inner = MergeNestedArguments(*CallObj);

			const FNexusCallCore::FResult Core = RunCapabilityCore(CapName, Inner, !bDeferFinalize);
			Result.ImageFiles.Append(Core.CapResult.ImageFiles);

			switch (Core.Status)
			{
			case FNexusCallCore::EStatus::Ok:
				Item->SetObjectField(TEXT("Data"), Core.TopOrWarn);
				++SuccessCount;
				if (bDeferFinalize && Core.Record->Def.Name.StartsWith(TEXT("ManageAsset")))
				{
					PendingFinalizes.Add({ Core, Inner, Item });
				}
				break;
			case FNexusCallCore::EStatus::Failed:
				Item->SetObjectField(TEXT("Data"), Core.TopOrWarn);
				Item->SetStringField(TEXT("Error"), TEXT("Capability reported operation or finalize errors; see Data."));
				Item->SetStringField(TEXT("ErrorKind"), TEXT("ExecutionFailed"));
				++FailureCount;
				break;
	case FNexusCallCore::EStatus::Unknown:
		Item->SetStringField(TEXT("Error"), FormatUnknownCapabilityError(Core.RequestedCapName, Core.CapName));
		Item->SetStringField(TEXT("ErrorKind"), TEXT("unknown"));
		if (!Core.RequestedCapName.Equals(Core.CapName, ESearchCase::IgnoreCase))
		{
			Item->SetStringField(TEXT("RequestedCapability"), Core.RequestedCapName);
		}
		Item->SetStringField(TEXT("FeedbackHint"), TEXT("SubmitFeedback(Category=\"WrongTool\")"));
		++FailureCount;
		break;
			case FNexusCallCore::EStatus::Disabled:
				Item->SetStringField(TEXT("Error"), FString::Printf(
					TEXT("Capability '%s' is disabled in settings."), *Core.Record->Def.Name));
				Item->SetStringField(TEXT("ErrorKind"), TEXT("disabled"));
				Item->SetStringField(TEXT("Hint"), DisabledHint(Core.Record));
				if (!Core.RequestedCapName.Equals(Core.Record->Def.Name, ESearchCase::IgnoreCase))
				{
					Item->SetStringField(TEXT("RequestedCapability"), Core.RequestedCapName);
				}
				++FailureCount;
				break;
			case FNexusCallCore::EStatus::Unavailable:
				Item->SetStringField(TEXT("Error"), FString::Printf(
					TEXT("Capability '%s' is unavailable on the current host (Dedicated Server / Game exposes runtime capabilities only)."),
					*Core.Record->Def.Name));
				Item->SetStringField(TEXT("ErrorKind"), TEXT("unavailable"));
				Item->SetStringField(TEXT("Hint"),
					TEXT("Do not retry. Use a runtime capability, or connect to a full Editor instance."));
				if (!Core.RequestedCapName.Equals(Core.Record->Def.Name, ESearchCase::IgnoreCase))
				{
					Item->SetStringField(TEXT("RequestedCapability"), Core.RequestedCapName);
				}
				++FailureCount;
				break;
		case FNexusCallCore::EStatus::ArgInvalid:
			{
				FString ErrMsg;
				Core.ArgInvalidErr->TryGetStringField(TEXT("Error"), ErrMsg);
				Item->SetStringField(TEXT("Error"), ErrMsg);
				Item->SetStringField(TEXT("ErrorKind"), TEXT("ArgInvalid"));
				FString Hint;
				if (Core.ArgInvalidErr->TryGetStringField(TEXT("Hint"), Hint) && !Hint.IsEmpty())
				{
					Item->SetStringField(TEXT("Hint"), Hint);
				}
				else
				{
					Item->SetStringField(TEXT("FeedbackHint"), TEXT("SubmitFeedback(Category=\"SchemaGuess\")"));
				}
				const TArray<TSharedPtr<FJsonValue>>* Par = nullptr;
				if (Core.ArgInvalidErr->TryGetArrayField(TEXT("Parameters"), Par) && Par)
				{
					Item->SetArrayField(TEXT("Parameters"), *Par);
				}
			}
			++FailureCount;
			break;
			case FNexusCallCore::EStatus::UserDenied:
				{
					FString ErrMsg;
					if (Core.ArgInvalidErr.IsValid())
					{
						Core.ArgInvalidErr->TryGetStringField(TEXT("Error"), ErrMsg);
					}
					Item->SetStringField(TEXT("Error"), ErrMsg);
					Item->SetStringField(TEXT("ErrorKind"), TEXT("UserDenied"));
					Item->SetStringField(TEXT("Hint"),
						TEXT("Do not retry; user denied this request."));
				}
				++FailureCount;
				break;
			case FNexusCallCore::EStatus::Fatal:
				Item->SetStringField(TEXT("Error"), Core.FatalMessage);
				Item->SetStringField(TEXT("ErrorKind"), TEXT("fatal"));
				Item->SetStringField(TEXT("FeedbackHint"), TEXT("SubmitFeedback(Category=\"WrongTool\")"));
				++FailureCount;
				break;
			}

			BatchResults.Add(MakeShared<FJsonValueObject>(Item));
		}

		// 两阶段收尾：业务失败不编译，任一编译失败不落盘。显式 Save/Delete 等包级副作用不在事务内。
		auto FinalizePending = [&](bool bSavePhase)
		{
			for (FPendingFinalize& Pending : PendingFinalizes)
			{
				TSharedPtr<FJsonObject> FinalizeArgs = MakeShared<FJsonObject>(*Pending.Arguments);
				FinalizeArgs->SetBoolField(bSavePhase ? TEXT("bCompile") : TEXT("bSaveToDisk"), false);
				Pending.Core.Record->Instance->Finalize(FinalizeArgs, Pending.Core.CapResult);
				if (Pending.Core.CapResult.HasErrors())
				{
					Pending.Item->SetStringField(TEXT("Error"), TEXT("Capability finalization failed; see Data."));
					Pending.Item->SetStringField(TEXT("ErrorKind"), TEXT("ExecutionFailed"));
					--SuccessCount;
					++FailureCount;
				}
			}
		};
		if (FailureCount == 0) FinalizePending(false);

		bool bRevertedBatch = false;
		const bool bMemoryPhaseFailed = FailureCount > 0;
		if (BatchTx.IsValid())
		{
			if (FailureCount > 0)
			{
				bRevertedBatch = BatchTx->IsOutstanding();
				FNexusEditorTransaction::CancelAndRevert(BatchTx);
			}
			BatchTx.Reset();
		}
		// 保存不参与内存事务；失败必须明确报告，不能宣称磁盘已整体回滚。
		if (FailureCount == 0) FinalizePending(true);

		TSharedPtr<FJsonObject> Top = MakeShared<FJsonObject>();
		Top->SetArrayField(TEXT("Results"), BatchResults);
		Top->SetNumberField(TEXT("SuccessCount"), SuccessCount);
		Top->SetNumberField(TEXT("FailureCount"), FailureCount);
		if (FailureCount > 0)
		{
			Top->SetBoolField(TEXT("bRolledBack"), bRevertedBatch);
			Top->SetStringField(TEXT("UndoNote"),
				!bDeferFinalize
					? TEXT("Single-item call failed; successful memory edits and package/runtime side effects are retained.")
					: bRevertedBatch
					? TEXT("Transactional memory edits rolled back; automatic saves skipped. Explicit package/runtime side effects are not reverted.")
					: bMemoryPhaseFailed
						? TEXT("Batch failed; automatic saves skipped. No outstanding editor transaction to revert.")
						: TEXT("Save phase failed after memory edits committed; disk writes are not atomic and were not rolled back.")
			);
		}
		Result.StructuredContent = Top;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Top);
		Result.bIsError = FailureCount > 0;
		if (Result.bIsError) Result.ErrorText = Result.OutputText;
		return Result;
	}

	// ── 单条形态（与历史响应兼容）────────────────────────────────────────────
	const FString CapName = Parsed.Str(TEXT("Capability"));
	if (CapName.IsEmpty())
	{
		Result.bIsError  = true;
		Result.ErrorText = TEXT("Missing or empty capability (use calls[] for batch)");
		return Result;
	}

	const TSharedPtr<FJsonObject> Inner = MergeNestedArguments(Args);
	const FNexusCallCore::FResult Core = RunCapabilityCore(CapName, Inner);
	Result.ImageFiles = Core.CapResult.ImageFiles;

	switch (Core.Status)
	{
	case FNexusCallCore::EStatus::Failed:
		Result.bIsError = true;
		Result.StructuredContent = Core.TopOrWarn;
		Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Core.TopOrWarn);
		return Result;
	case FNexusCallCore::EStatus::Unknown:
		{
			Result.bIsError = true;
			TSharedPtr<FJsonObject> Err = BuildCallErrorObject(
				TEXT("unknown"),
				FormatUnknownCapabilityError(Core.RequestedCapName, Core.CapName),
				FString(),
				TEXT("Use SearchCapabilities to find the canonical name; do not repeat CallCapability."),
				Core.RequestedCapName);
			Err->SetStringField(TEXT("FeedbackHint"),
				TEXT("SubmitFeedback(Category=\"WrongTool\")"));
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
				DisabledHint(Core.Record),
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
	case FNexusCallCore::EStatus::ArgInvalid:
		{
			Result.bIsError = true;
			FString Hint;
			if (!Core.ArgInvalidErr->TryGetStringField(TEXT("Hint"), Hint) || Hint.IsEmpty())
			{
				Core.ArgInvalidErr->SetStringField(TEXT("FeedbackHint"),
					TEXT("SubmitFeedback(Category=\"SchemaGuess\")"));
			}
			Result.StructuredContent = Core.ArgInvalidErr;
			Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Core.ArgInvalidErr);
		}
		return Result;
	case FNexusCallCore::EStatus::UserDenied:
		{
			Result.bIsError = true;
			Result.StructuredContent = Core.ArgInvalidErr;
			Result.ErrorText = FNexusJsonUtils::SerializeCondensed(Core.ArgInvalidErr);
		}
		return Result;
	case FNexusCallCore::EStatus::Fatal:
		Result.bIsError  = true;
		Result.ErrorText = Core.FatalMessage + TEXT("\n→ SubmitFeedback(Category=\"WrongTool\")");
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
