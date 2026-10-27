// Copyright byteyang. All Rights Reserved.

#include "Tools/NexusMcpToolSearchCapabilities.h"
#include "NexusFeedback.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusMcpToolRegistry.h"
#include "NexusMcpTool.h"
#include "Utils/NexusCapabilityIndexUtils.h"
#include "Utils/NexusHostUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FNexusCapabilityLookup final
{
	FNexusCapabilityLookup() = delete;

	enum class EStatus : uint8
	{
		Enabled,
		NotFound,
		Disabled,
		Unavailable
	};
};

/** 按名解析（大小写不敏感）；与 CallCapability 一致区分不存在 vs 设置禁用 vs 宿主不可用。 */
static FNexusCapabilityLookup::EStatus ResolveCapabilityByName(
		const FString& Name, const UNexusLinkSettings* Settings, const FCapRecord*& OutRecord)
{
	if (!Settings)
	{
		return FNexusCapabilityLookup::EStatus::NotFound;
	}
	OutRecord = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
	if (!OutRecord)
	{
		return FNexusCapabilityLookup::EStatus::NotFound;
	}
	if (!FNexusHostUtils::IsCapabilityVisibleOnHost(*OutRecord))
	{
		return FNexusCapabilityLookup::EStatus::Unavailable;
	}
	if (!Settings->IsCapabilityEnabled(OutRecord->Def.Name))
	{
		return FNexusCapabilityLookup::EStatus::Disabled;
	}
	return FNexusCapabilityLookup::EStatus::Enabled;
}

static void EmitCapabilityDetailFromRecord(const FCapRecord& Record, TSharedPtr<FJsonObject>& Output)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"),        Record.Def.Name);
	O->SetStringField(TEXT("description"), Record.Def.Description);
	O->SetArrayField(TEXT("Parameters"),   FNexusCapabilityIndexUtils::ExtractParameters(Record.Def.InputSchema));
	if (Record.Def.OutputSchema.IsValid()) O->SetObjectField(TEXT("outputSchema"), Record.Def.OutputSchema);
	FNexusCapabilityIndexUtils::AttachMetaHints(O, Record.Def);
	Output->SetObjectField(TEXT("Capability"), O);
}

static void EmitCapabilityLookupError(FNexusCapabilityLookup::EStatus Status, const FString& RequestedName,
	                                      const FCapRecord* Record, TSharedPtr<FJsonObject>& Output)
{
	if (Status == FNexusCapabilityLookup::EStatus::NotFound)
	{
		Output->SetStringField(TEXT("ErrorKind"), TEXT("NotFound"));
		Output->SetStringField(TEXT("Error"),
				FString::Printf(TEXT("Capability '%s' does not exist."), *RequestedName));
		return;
	}
	check(Record);
	if (Status == FNexusCapabilityLookup::EStatus::Unavailable)
	{
		Output->SetStringField(TEXT("ErrorKind"), TEXT("unavailable"));
		Output->SetStringField(TEXT("CapabilityName"), Record->Def.Name);
		Output->SetStringField(TEXT("Error"),
				FString::Printf(
					TEXT("Capability '%s' is unavailable on the current host (Dedicated Server / Game exposes runtime capabilities only)."),
					*Record->Def.Name));
		return;
	}
	Output->SetStringField(TEXT("ErrorKind"), TEXT("disabled"));
	Output->SetStringField(TEXT("CapabilityName"), Record->Def.Name);
	Output->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("Capability '%s' is disabled in settings."), *Record->Def.Name));
		if (Record->Def.HasTag(FNexusMcpTags::Dangerous))
		{
			Output->SetStringField(TEXT("Hint"),
				TEXT("Set Editor Preferences → NexusLink → Dangerous Capability access to Confirm or Custom."));
		}
}

/** 模糊搜索：在已禁用 cap 中找与 query 匹配的候选（enabled 结果为空时提示用）。 */
static void AppendDisabledCapabilityMatches(const TArray<FString>& Tokens, bool bRelaxed,
	                                            const UNexusLinkSettings* Settings,
	                                            TArray<TSharedPtr<FJsonValue>>& OutArr, int32 MaxResults = 5)
{
	if (!Settings) return;

	struct FScored
	{
		int32 Score = 0;
		TSharedPtr<FJsonObject> Entry;
	};
	TArray<FScored> Scored;

	for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		if (Settings->IsCapabilityEnabled(Record.Def.Name)) continue;
		if (!FNexusHostUtils::IsCapabilityVisibleOnHost(Record)) continue;

		int32 Score = 0;
		int32 Matched = 0;
		if (bRelaxed)
		{
			Score = FNexusCapabilityIndexUtils::ScoreCapabilityPartial(Tokens, Record.Keywords, Matched);
		}
		else
		{
			Score = FNexusCapabilityIndexUtils::ScoreCapability(Tokens, Record.Keywords);
		}
		if (Score <= 0) continue;

		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("name"),        Record.Def.Name);
		E->SetStringField(TEXT("description"), Record.Def.Description);
		E->SetNumberField(TEXT("Score"),       Score);
		if (bRelaxed)
		{
			E->SetNumberField(TEXT("MatchedTokens"), Matched);
		}
		Scored.Add({ Score, E });
	}

	Scored.StableSort([](const FScored& A, const FScored& B) { return A.Score > B.Score; });
	if (MaxResults > 0 && Scored.Num() > MaxResults)
	{
		Scored.SetNum(MaxResults);
	}
	for (const FScored& S : Scored)
	{
		OutArr.Add(MakeShared<FJsonValueObject>(S.Entry));
	}
}
/** 单 token 过宽词：直接拒绝模糊搜索，迫使 Agent 收窄 query。 */
static bool IsOverBroadCapabilityQuery(const FString& TokenLower, TArray<FString>& OutSuggested)
{
	OutSuggested.Reset();
	if (TokenLower == TEXT("blueprint"))
	{
		OutSuggested = {
			TEXT("blueprint graph"), TEXT("blueprint variable"), TEXT("GetAssetBlueprint")
		};
		return true;
	}
	if (TokenLower == TEXT("Asset"))
	{
		OutSuggested = {
			TEXT("search asset"), TEXT("get asset"), TEXT("manage asset")
		};
		return true;
	}
	if (TokenLower == TEXT("Runtime"))
	{
		OutSuggested = {
			TEXT("runtime actor"), TEXT("runtime widget"), TEXT("runtime niagara")
		};
		return true;
	}
	if (TokenLower == TEXT("Animation"))
	{
		OutSuggested = {
			TEXT("runtime animation"), TEXT("anim montage"), TEXT("anim blueprint")
		};
		return true;
	}
	return false;
}

static bool TryGatedPluginHint(const FString& QueryLower, FString& OutHint)
{
	struct FGate { const TCHAR* Needle; const TCHAR* Hint; };
	static const FGate Gates[] = {
		{ TEXT("niagara"),    TEXT("Niagara capabilities require the Niagara plugin; not registered on this host—do not retry. Use Query=\"\" to list the local catalog.") },
		{ TEXT("gameplay"),   TEXT("GAS capabilities require GameplayAbilities; not registered on this host—do not retry. Use GetGameplayTags for tag queries.") },
		{ TEXT("statetree"),  TEXT("StateTree requires UE 5.5+ and the plugin; not registered on this host—do not retry.") },
		{ TEXT("state tree"), TEXT("StateTree requires UE 5.5+ and the plugin; not registered on this host—do not retry.") },
		{ TEXT("mvvm"),       TEXT("MVVM / ViewModel requires UE 5.5+; not registered on this host—do not retry.") },
		{ TEXT("viewmodel"),  TEXT("MVVM / ViewModel requires UE 5.5+; not registered on this host—do not retry.") },
		{ TEXT("view model"), TEXT("MVVM / ViewModel requires UE 5.5+; not registered on this host—do not retry.") },
		{ TEXT("metasound"),  TEXT("MetaSound requires UE 5.0+; not registered on this host—do not retry.") },
		{ TEXT("controlrig"), TEXT("ControlRig requires UE 5.0+ and the plugin; not registered on this host—do not retry.") },
		{ TEXT("control rig"),TEXT("ControlRig requires UE 5.0+ and the plugin; not registered on this host—do not retry.") },
		{ TEXT("eqs"),        TEXT("EQS capabilities require UE5+; not registered on this host—do not retry.") },
		{ TEXT("pcg"),        TEXT("PCG capabilities require UE 5.4+; not registered on this host—do not retry.") },
	};
	for (const FGate& G : Gates)
	{
		if (QueryLower.Contains(G.Needle))
		{
			OutHint = G.Hint;
			return true;
		}
	}
	return false;
}

static bool TryIntentHint(const FString& QueryLower, FString& OutHint)
{
	if (QueryLower.Contains(TEXT("cpp enum"))
		|| QueryLower.Contains(TEXT("uenum"))
		|| QueryLower.Contains(TEXT("class reflection"))
		|| QueryLower.Contains(TEXT("native enum")))
	{
		OutHint = TEXT("Native C++ UENUM / class reflection is out of scope. Use GetAssetEnum for UserDefinedEnum assets.");
		return true;
	}
	return false;
}

static void EmitSuggestedQueries(TSharedPtr<FJsonObject>& Output, const TArray<FString>& Suggested)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& Q : Suggested)
	{
		Arr.Add(MakeShared<FJsonValueString>(Q));
	}
	Output->SetArrayField(TEXT("SuggestedQueries"), Arr);
}

void FNexusMcpToolSearchCapabilities::BuildDefinition(FNexusMcpToolDefinition& Out) const
{
	Out.Name        = TEXT("SearchCapabilities");
	Out.Description = TEXT("[Stage 3 - Discover] Find available capabilities—the first step for any UE operation.\nTrigger: user mentions UE/Blueprint/Widget/UMG/Material/Asset/BehaviorTree/ABP/DataAsset/GAS/Niagara/Level/PIE/Actor; call after instance discovery (read-only, no connect required).\nUsage: known name → CapabilityName=<exact>; unknown → Query=<narrow 1-2 words, e.g. blueprint variable>. Do not use blueprint/asset/runtime/animation alone. <=2 matches return full parameters[].\nConstraints: on failure check ErrorKind (NotFound/disabled/unavailable/QueryTooBroad); FeedbackHint requires immediate SubmitFeedback.");

	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> QueryProp = MakeShared<FJsonObject>();
	QueryProp->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetStringField(TEXT("description"),
		TEXT("Narrow keywords (e.g. blueprint variable). Do not use blueprint/asset/runtime/animation alone; use CapabilityName for exact names."));

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"),
		TEXT("Exact capability name (e.g. GetAssetBlueprint). Returns full parameter list when provided."));

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(TEXT("Query"),          QueryProp);
	Props->SetObjectField(TEXT("CapabilityName"), NameProp);
	Schema->SetObjectField(TEXT("properties"), Props);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("Query")));
	Schema->SetArrayField(TEXT("required"), Required);

	Out.InputSchema = Schema;
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
}

// ── Execute ──────────────────────────────────────────────────────────────

FNexusMcpToolResult FNexusMcpToolSearchCapabilities::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const FNexusArgs Parsed(Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>());

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();

	if (!Settings)
	{
		Output->SetStringField(TEXT("ErrorKind"), TEXT("internal"));
		Output->SetStringField(TEXT("Error"), TEXT("NexusLink settings unavailable (engine may be shutting down)."));
		Result.StructuredContent = Output;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
		return Result;
	}

	// ── CapabilityName 精确查询：直接返回完整参数列表 ──────────────────────────
	const FString CapabilityName = Parsed.Str(TEXT("CapabilityName"));
	if (!CapabilityName.IsEmpty())
	{
		const FCapRecord* Record = nullptr;
		const FNexusCapabilityLookup::EStatus Status = ResolveCapabilityByName(CapabilityName, Settings, Record);
		if (Status == FNexusCapabilityLookup::EStatus::Enabled)
		{
			EmitCapabilityDetailFromRecord(*Record, Output);
			Result.StructuredContent = Output;
			Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
			return Result;
		}
		EmitCapabilityLookupError(Status, CapabilityName, Record, Output);
		Result.StructuredContent = Output;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
		return Result;
	}

	// ── Query="" 目录模式：返回所有 cap 按 tag 分组 ────────────────────────────
	const FString QueryRaw = Parsed.Str(TEXT("Query"));

	if (QueryRaw.TrimStartAndEnd().IsEmpty())
	{
		Output->SetStringField(TEXT("Hint"),
			TEXT("Capability catalog (names only). Use CapabilityName=<name> for description and parameter schema."));
		Output->SetObjectField(TEXT("Directory"), FNexusCapabilityIndexUtils::BuildDirectory(Settings));
		Result.StructuredContent = Output;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
		return Result;
	}

	// ── query 精确 cap 名短路（等同 CapabilityName）────────────────────────────
	const FString QueryTrimmed = QueryRaw.TrimStartAndEnd();
	{
		const FCapRecord* Record = nullptr;
		const FNexusCapabilityLookup::EStatus Status = ResolveCapabilityByName(QueryTrimmed, Settings, Record);
		if (Status == FNexusCapabilityLookup::EStatus::Enabled)
		{
			EmitCapabilityDetailFromRecord(*Record, Output);
			Output->SetStringField(TEXT("Hint"),
				TEXT("Exact capability name match; full parameter schema returned."));
			Result.StructuredContent = Output;
			Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
			return Result;
		}
		if (Status == FNexusCapabilityLookup::EStatus::Disabled || Status == FNexusCapabilityLookup::EStatus::Unavailable)
		{
			EmitCapabilityLookupError(Status, QueryTrimmed, Record, Output);
			Result.StructuredContent = Output;
			Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
			return Result;
		}
	}

	// ── 单 token 过宽词硬拦截（避免 category tag 命中整类导致 overflow）──────────
	{
		TArray<FString> GateTokens;
		QueryTrimmed.ToLower().ParseIntoArrayWS(GateTokens);
		TArray<FString> Suggested;
		if (GateTokens.Num() == 1 && IsOverBroadCapabilityQuery(GateTokens[0], Suggested))
		{
			Output->SetStringField(TEXT("ErrorKind"), TEXT("QueryTooBroad"));
			Output->SetNumberField(TEXT("TotalCount"), 0);
			Output->SetArrayField(TEXT("Capabilities"), TArray<TSharedPtr<FJsonValue>>());
			Output->SetStringField(TEXT("Hint"), FString::Printf(
				TEXT("Query=\"%s\" is too broad (matches entire categories). Use a narrow term from SuggestedQueries, or CapabilityName=<exact name>."),
				*GateTokens[0]));
			EmitSuggestedQueries(Output, Suggested);
			Output->SetStringField(TEXT("FeedbackHint"),
				TEXT("SubmitFeedback(Category=\"SearchOverflow\")"));
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("SearchCapabilities");
			F.Query      = QueryRaw;
			F.MatchCount = 0;
			F.Note       = TEXT("QueryTooBroad");
			FNexusFeedback::RecordAuto(TEXT("SearchOverflow"), F);
			Result.StructuredContent = Output;
			Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
			return Result;
		}
	}

	// ── query 模糊搜索（AND 语义）──────────────────────────────────────────────
	TArray<FString> Tokens;
	QueryTrimmed.ToLower().ParseIntoArrayWS(Tokens);

	struct FScored
	{
		int32 Score = 0;
		int32 MatchedTokens = 0;
		TSharedPtr<FJsonObject> Entry;
		const FNexusCapabilityDefinition* Def = nullptr;
	};
	TArray<FScored> Scored;
	bool bRelaxedMatch = false;

	for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		if (!Settings->IsCapabilityEnabled(Record.Def.Name)) continue;
		if (!FNexusHostUtils::IsCapabilityVisibleOnHost(Record)) continue;
		const int32 Score = FNexusCapabilityIndexUtils::ScoreCapability(Tokens, Record.Keywords);
		if (Score <= 0) continue;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("name"),        Record.Def.Name);
		E->SetStringField(TEXT("description"), Record.Def.Description);
		E->SetNumberField(TEXT("Score"),       Score);
		E->SetField(TEXT("Schema"),
			MakeShared<FJsonValueObject>(Record.Def.InputSchema.IsValid() ? Record.Def.InputSchema : MakeShared<FJsonObject>()));
		Scored.Add({ Score, Tokens.Num(), E, &Record.Def });
	}

	// AND 零命中 → OR 部分匹配降级（Top 3）
	if (Scored.Num() == 0 && Tokens.Num() > 0)
	{
		bRelaxedMatch = true;
		for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
		{
			if (!Settings->IsCapabilityEnabled(Record.Def.Name)) continue;
			if (!FNexusHostUtils::IsCapabilityVisibleOnHost(Record)) continue;
			int32 Matched = 0;
			const int32 Score = FNexusCapabilityIndexUtils::ScoreCapabilityPartial(Tokens, Record.Keywords, Matched);
			if (Score <= 0) continue;
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("name"),        Record.Def.Name);
			E->SetStringField(TEXT("description"), Record.Def.Description);
			E->SetNumberField(TEXT("Score"),       Score);
			E->SetNumberField(TEXT("MatchedTokens"), Matched);
			E->SetField(TEXT("Schema"),
				MakeShared<FJsonValueObject>(Record.Def.InputSchema.IsValid() ? Record.Def.InputSchema : MakeShared<FJsonObject>()));
			Scored.Add({ Score, Matched, E, &Record.Def });
		}
		Scored.StableSort([](const FScored& A, const FScored& B){ return A.Score > B.Score; });
		const int32 RelaxedMax = 3;
		if (Scored.Num() > RelaxedMax)
			Scored.SetNum(RelaxedMax);
	}

	Scored.StableSort([](const FScored& A, const FScored& B){ return A.Score > B.Score; });

	const int32 TotalBeforeTrunc = Scored.Num();
	const int32 MaxResults = Settings->MaxSearchResults;
	if (!bRelaxedMatch && MaxResults > 0 && Scored.Num() > MaxResults)
		Scored.SetNum(MaxResults);

	const bool bFull = Scored.Num() <= 2;
	TSet<FString> HitNames;
	for (const FScored& S : Scored)
	{
		if (S.Def)
		{
			HitNames.Add(S.Def->Name);
		}
	}

	TArray<TSharedPtr<FJsonValue>> CapArr;
	for (FScored& S : Scored)
	{
		if (bFull)
		{
			const TSharedPtr<FJsonObject>* SchemaObj = nullptr;
			if (S.Entry->TryGetObjectField(TEXT("Schema"), SchemaObj) && SchemaObj)
				S.Entry->SetArrayField(TEXT("Parameters"), FNexusCapabilityIndexUtils::ExtractParameters(*SchemaObj));
		}
		else
		{
			if (S.Def)
			{
				if (!S.Def->WhenToUse.IsEmpty())
					S.Entry->SetStringField(TEXT("WhenToUse"), S.Def->WhenToUse);
				// 多命中：related 去重（已在本页结果中的名不重复列），最多 3 条
				if (S.Def->RelatedCapabilities.Num() > 0)
				{
					const TArray<FString> Related = FNexusCapabilityIndexUtils::FilterVisibleRelated(
						S.Def->RelatedCapabilities, Settings, 8);
					TArray<TSharedPtr<FJsonValue>> Arr;
					for (const FString& R : Related)
					{
						if (HitNames.Contains(R))
						{
							continue;
						}
						Arr.Add(MakeShared<FJsonValueString>(R));
						if (Arr.Num() >= 3)
						{
							break;
						}
					}
					if (Arr.Num() > 0)
					{
						S.Entry->SetArrayField(TEXT("RelatedCapabilities"), Arr);
					}
				}
			}
		}
		if (bFull && S.Def) FNexusCapabilityIndexUtils::AttachMetaHints(S.Entry, *S.Def);
        if (S.Def && S.Def->OutputSchema.IsValid()) S.Entry->SetObjectField(TEXT("outputSchema"), S.Def->OutputSchema);
        if (S.Def && S.Def->ExecutionInfo) S.Entry->SetObjectField(TEXT("Execution"), S.Def->ExecutionInfo());
		S.Entry->RemoveField(TEXT("Schema"));
		CapArr.Add(MakeShared<FJsonValueObject>(S.Entry));
	}

	Output->SetNumberField(TEXT("TotalCount"),  TotalBeforeTrunc);
	Output->SetArrayField(TEXT("Capabilities"), CapArr);
	if (bRelaxedMatch)
		Output->SetBoolField(TEXT("bRelaxedMatch"), true);
	if (TotalBeforeTrunc > CapArr.Num())
		Output->SetBoolField(TEXT("bTruncated"), true);

	if (CapArr.Num() == 0 && Tokens.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> DisabledArr;
		AppendDisabledCapabilityMatches(Tokens, bRelaxedMatch, Settings, DisabledArr);
		if (DisabledArr.Num() > 0)
		{
			Output->SetStringField(TEXT("ErrorKind"), TEXT("DisabledOnly"));
			Output->SetStringField(TEXT("Hint"),
				TEXT("No enabled matches; names below exist but are disabled in NexusLink settings. For dangerous caps set access mode to Confirm or Custom in Editor Preferences, or Query=\"\" for the enabled catalog."));
			Output->SetArrayField(TEXT("DisabledCapabilities"), DisabledArr);
		}
		else
		{
			FString Hint = TEXT("No matching capability. Try Query=\"\" for the full catalog, or CapabilityName=<exact name>.");
			if (QueryRaw.Equals(TEXT("GetAsset"), ESearchCase::IgnoreCase))
			{
				Hint = TEXT("No GetAsset aggregate tool; use get_asset_<type> (e.g. GetAssetBlueprint) or SearchAsset for paths.");
			}
			else
			{
				FString GateHint;
				if (TryGatedPluginHint(QueryRaw.ToLower(), GateHint))
				{
					Hint = GateHint;
				}
				else if (TryIntentHint(QueryRaw.ToLower(), GateHint))
				{
					Hint = GateHint;
				}
				else if (!FNexusHostUtils::IsFullEditorCapabilityHost())
				{
					Hint = TEXT("No matching capability on this Game/DS host (runtime caps only). Use Query=\"\" for the local catalog; connect to an Editor instance (list_unreal_instances HostKind=Editor) for asset tools.");
				}
			}
			Output->SetStringField(TEXT("ErrorKind"), TEXT("NotFound"));
			Output->SetStringField(TEXT("Hint"), Hint);
			Output->SetStringField(TEXT("FeedbackHint"),
				TEXT("SubmitFeedback(Category=\"SearchZero\")"));
		}

		static const TArray<FString> FallbackCaps = {
			TEXT("SearchAsset"),
			TEXT("GetAssetBlueprint"),
			TEXT("GetAssetGameplayAbility"),
			TEXT("GetRuntimeActorProperty"),
		};
		TArray<TSharedPtr<FJsonValue>> TopArr;
		for (const FString& FbName : FallbackCaps)
		{
			const FCapRecord* R = FNexusCapabilityRegistry::Get().FindRecordByName(FbName);
			if (R && Settings->IsCapabilityEnabled(R->Def.Name)
				&& FNexusHostUtils::IsCapabilityVisibleOnHost(*R))
			{
				TSharedPtr<FJsonObject> FbEntry = MakeShared<FJsonObject>();
				FbEntry->SetStringField(TEXT("name"),        R->Def.Name);
				FbEntry->SetStringField(TEXT("description"), R->Def.Description);
				TopArr.Add(MakeShared<FJsonValueObject>(FbEntry));
			}
		}
		if (TopArr.Num() > 0)
			Output->SetArrayField(TEXT("RoutingHints"), TopArr);

		FNexusFeedback::FFields F;
		F.Tool  = TEXT("SearchCapabilities");
		F.Query = QueryRaw;
		FNexusFeedback::RecordAuto(TEXT("SearchZero"), F);
	}
	else if (bRelaxedMatch)
	{
		Output->SetStringField(TEXT("Hint"),
			TEXT("Strict AND found no match; showing partial-match top results (bRelaxedMatch). Narrow query or pass CapabilityName=<exact name>."));
		Output->SetStringField(TEXT("FeedbackHint"),
			TEXT("SubmitFeedback(Category=\"SearchZero\")"));
	}
	else if (!bFull)
	{
		const bool bWasTruncated = TotalBeforeTrunc > CapArr.Num();
		if (bWasTruncated)
		{
			Output->SetStringField(TEXT("Hint"), FString::Printf(
				TEXT("Too many results (%d total, showing first %d). Narrow query to 1-2 precise words, or pass CapabilityName=<exact name>."),
				TotalBeforeTrunc, CapArr.Num()));

			// 只在真被截断（结果超出 MaxSearchResults 未能全部返回）时才算 overflow；
			// 6~8 命中但未截断（SearchOverflowThreshold < MaxSearchResults）时结果完整可用，不应逼迫上报
			if (TotalBeforeTrunc > Settings->SearchOverflowThreshold)
			{
				FNexusFeedback::FFields F;
				F.Tool       = TEXT("SearchCapabilities");
				F.Query      = QueryRaw;
				F.MatchCount = TotalBeforeTrunc;
				FNexusFeedback::RecordAuto(TEXT("SearchOverflow"), F);
				Output->SetStringField(TEXT("FeedbackHint"),
					TEXT("SubmitFeedback(Category=\"SearchOverflow\")"));
			}
		}
		else
		{
			Output->SetStringField(TEXT("Hint"),
				TEXT("Multiple capabilities matched. Check WhenToUse/RelatedCapabilities, or pass CapabilityName=<exact name> for full parameters."));
		}
	}

	{
		FString IntentHint;
		if (TryIntentHint(QueryTrimmed.ToLower(), IntentHint))
		{
			Output->SetStringField(TEXT("Hint"), IntentHint);
		}
	}

	Result.StructuredContent = Output;
	Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolSearchCapabilities)
