// Copyright byteyang. All Rights Reserved.

#include "Tools/NexusMcpToolSearchCapabilities.h"
#include "NexusFeedback.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusMcpToolRegistry.h"
#include "Utils/NexusCapabilityIndexUtils.h"
#include "Utils/NexusHostUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	enum class ECapabilityLookupStatus : uint8
	{
		Enabled,
		NotFound,
		Disabled,
		Unavailable
	};

	/** 按名解析（大小写不敏感）；与 call_capability 一致区分不存在 vs 设置禁用 vs 宿主不可用。 */
	static ECapabilityLookupStatus ResolveCapabilityByName(
		const FString& Name, const UNexusLinkSettings* Settings, const FCapRecord*& OutRecord)
	{
		if (!Settings)
		{
			return ECapabilityLookupStatus::NotFound;
		}
		OutRecord = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
		if (!OutRecord)
		{
			return ECapabilityLookupStatus::NotFound;
		}
		if (!FNexusHostUtils::IsCapabilityVisibleOnHost(*OutRecord))
		{
			return ECapabilityLookupStatus::Unavailable;
		}
		if (!Settings->IsCapabilityEnabled(OutRecord->Def.Name))
		{
			return ECapabilityLookupStatus::Disabled;
		}
		return ECapabilityLookupStatus::Enabled;
	}

	static void EmitCapabilityDetailFromRecord(const FCapRecord& Record, TSharedPtr<FJsonObject>& Output)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),        Record.Def.Name);
		O->SetStringField(TEXT("description"), Record.Def.Description);
		O->SetArrayField(TEXT("parameters"),   FNexusCapabilityIndexUtils::ExtractParameters(Record.Def.InputSchema));
		FNexusCapabilityIndexUtils::AttachMetaHints(O, Record.Def);
		Output->SetObjectField(TEXT("capability"), O);
	}

	static void EmitCapabilityLookupError(ECapabilityLookupStatus Status, const FString& RequestedName,
	                                      const FCapRecord* Record, TSharedPtr<FJsonObject>& Output)
	{
		if (Status == ECapabilityLookupStatus::NotFound)
		{
			Output->SetStringField(TEXT("errorKind"), TEXT("not_found"));
			Output->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Capability '%s' does not exist."), *RequestedName));
			return;
		}
		check(Record);
		if (Status == ECapabilityLookupStatus::Unavailable)
		{
			Output->SetStringField(TEXT("errorKind"), TEXT("unavailable"));
			Output->SetStringField(TEXT("capabilityName"), Record->Def.Name);
			Output->SetStringField(TEXT("error"),
				FString::Printf(
					TEXT("Capability '%s' is unavailable on the current host (Dedicated Server / Game exposes runtime capabilities only)."),
					*Record->Def.Name));
			return;
		}
		Output->SetStringField(TEXT("errorKind"), TEXT("disabled"));
		Output->SetStringField(TEXT("capabilityName"), Record->Def.Name);
		Output->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Capability '%s' is disabled in settings."), *Record->Def.Name));
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
			E->SetNumberField(TEXT("score"),       Score);
			if (bRelaxed)
			{
				E->SetNumberField(TEXT("matchedTokens"), Matched);
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
				TEXT("blueprint graph"), TEXT("blueprint variable"), TEXT("get_asset_blueprint")
			};
			return true;
		}
		if (TokenLower == TEXT("asset"))
		{
			OutSuggested = {
				TEXT("search asset"), TEXT("get asset"), TEXT("manage asset")
			};
			return true;
		}
		if (TokenLower == TEXT("runtime"))
		{
			OutSuggested = {
				TEXT("runtime actor"), TEXT("runtime widget"), TEXT("runtime lua")
			};
			return true;
		}
		if (TokenLower == TEXT("animation"))
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
			{ TEXT("niagara"),    TEXT("Niagara capabilities require the Niagara plugin; not registered on this host—do not retry. Use query=\"\" to list the local catalog.") },
			{ TEXT("gameplay"),   TEXT("GAS capabilities require GameplayAbilities; not registered on this host—do not retry. Use get_gameplay_tags for tag queries.") },
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

	static void EmitSuggestedQueries(TSharedPtr<FJsonObject>& Output, const TArray<FString>& Suggested)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& Q : Suggested)
		{
			Arr.Add(MakeShared<FJsonValueString>(Q));
		}
		Output->SetArrayField(TEXT("suggestedQueries"), Arr);
	}
} // namespace

void FNexusMcpToolSearchCapabilities::BuildDefinition(FNexusMcpToolDefinition& Out) const
{
	Out.Name        = TEXT("search_capabilities");
	Out.Description = TEXT("[Stage 3 - Discover] Find available capabilities—the first step for any UE operation.\nTrigger: user mentions UE/Blueprint/Widget/UMG/Material/Asset/BehaviorTree/ABP/DataAsset/GAS/Niagara/Level/PIE/Actor; call after instance discovery (read-only, no connect required).\nUsage: known name → capabilityName=<exact>; unknown → query=<narrow 1-2 words, e.g. blueprint graph>. Do not use blueprint/asset/runtime/animation alone. <=2 matches return full parameters[].\nConstraints: on failure check errorKind (not_found/disabled/unavailable/query_too_broad); _feedbackHint requires immediate submit_feedback.");

	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> QueryProp = MakeShared<FJsonObject>();
	QueryProp->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetStringField(TEXT("description"),
		TEXT("Narrow keywords (e.g. blueprint graph). Do not use blueprint/asset/runtime/animation alone; use capabilityName for exact names."));

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"),
		TEXT("Exact capability name (e.g. get_asset_blueprint). Returns full parameter list when provided."));

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(TEXT("query"),          QueryProp);
	Props->SetObjectField(TEXT("capabilityName"), NameProp);
	Schema->SetObjectField(TEXT("properties"), Props);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("query")));
	Schema->SetArrayField(TEXT("required"), Required);

	Out.InputSchema = Schema;
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
}

// ── Execute ──────────────────────────────────────────────────────────────

FNexusMcpToolResult FNexusMcpToolSearchCapabilities::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();

	const UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
	TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();

	if (!Settings)
	{
		Output->SetStringField(TEXT("errorKind"), TEXT("internal"));
		Output->SetStringField(TEXT("error"), TEXT("NexusLink settings unavailable (engine may be shutting down)."));
		Result.StructuredContent = Output;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
		return Result;
	}

	// ── capabilityName 精确查询：直接返回完整参数列表 ──────────────────────────
	FString CapabilityName;
	if (Args->TryGetStringField(TEXT("capabilityName"), CapabilityName) && !CapabilityName.IsEmpty())
	{
		const FCapRecord* Record = nullptr;
		const ECapabilityLookupStatus Status = ResolveCapabilityByName(CapabilityName, Settings, Record);
		if (Status == ECapabilityLookupStatus::Enabled)
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

	// ── query="" 目录模式：返回所有 cap 按 tag 分组 ────────────────────────────
	FString QueryRaw;
	Args->TryGetStringField(TEXT("query"), QueryRaw);

	if (QueryRaw.TrimStartAndEnd().IsEmpty())
	{
		Output->SetStringField(TEXT("hint"),
			TEXT("Capability catalog (names only). Use capabilityName=<name> for description and parameter schema."));
		Output->SetObjectField(TEXT("directory"), FNexusCapabilityIndexUtils::BuildDirectory(Settings));
		Result.StructuredContent = Output;
		Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
		return Result;
	}

	// ── query 精确 cap 名短路（等同 capabilityName）────────────────────────────
	const FString QueryTrimmed = QueryRaw.TrimStartAndEnd();
	{
		const FCapRecord* Record = nullptr;
		const ECapabilityLookupStatus Status = ResolveCapabilityByName(QueryTrimmed, Settings, Record);
		if (Status == ECapabilityLookupStatus::Enabled)
		{
			EmitCapabilityDetailFromRecord(*Record, Output);
			Output->SetStringField(TEXT("hint"),
				TEXT("Exact capability name match; full parameter schema returned."));
			Result.StructuredContent = Output;
			Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
			return Result;
		}
		if (Status == ECapabilityLookupStatus::Disabled || Status == ECapabilityLookupStatus::Unavailable)
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
			Output->SetStringField(TEXT("errorKind"), TEXT("query_too_broad"));
			Output->SetNumberField(TEXT("totalCount"), 0);
			Output->SetArrayField(TEXT("capabilities"), TArray<TSharedPtr<FJsonValue>>());
			Output->SetStringField(TEXT("hint"), FString::Printf(
				TEXT("query=\"%s\" is too broad (matches entire categories). Use a narrow term from suggestedQueries, or capabilityName=<exact name>."),
				*GateTokens[0]));
			EmitSuggestedQueries(Output, Suggested);
			Output->SetStringField(TEXT("_feedbackHint"),
				TEXT("submit_feedback(category=\"search_overflow\")"));
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("search_capabilities");
			F.Query      = QueryRaw;
			F.MatchCount = 0;
			F.Note       = TEXT("query_too_broad");
			FNexusFeedback::RecordAuto(TEXT("search_overflow"), F);
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
		E->SetNumberField(TEXT("score"),       Score);
		E->SetField(TEXT("_schema"),
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
			E->SetNumberField(TEXT("score"),       Score);
			E->SetNumberField(TEXT("matchedTokens"), Matched);
			E->SetField(TEXT("_schema"),
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
			if (S.Entry->TryGetObjectField(TEXT("_schema"), SchemaObj) && SchemaObj)
				S.Entry->SetArrayField(TEXT("parameters"), FNexusCapabilityIndexUtils::ExtractParameters(*SchemaObj));
		}
		else
		{
			if (S.Def)
			{
				if (!S.Def->WhenToUse.IsEmpty())
					S.Entry->SetStringField(TEXT("whenToUse"), S.Def->WhenToUse);
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
						S.Entry->SetArrayField(TEXT("relatedCapabilities"), Arr);
					}
				}
			}
		}
		if (bFull && S.Def) FNexusCapabilityIndexUtils::AttachMetaHints(S.Entry, *S.Def);
		S.Entry->RemoveField(TEXT("_schema"));
		CapArr.Add(MakeShared<FJsonValueObject>(S.Entry));
	}

	Output->SetNumberField(TEXT("totalCount"),  TotalBeforeTrunc);
	Output->SetArrayField(TEXT("capabilities"), CapArr);
	if (bRelaxedMatch)
		Output->SetBoolField(TEXT("relaxedMatch"), true);
	if (TotalBeforeTrunc > CapArr.Num())
		Output->SetBoolField(TEXT("truncated"), true);

	if (CapArr.Num() == 0 && Tokens.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> DisabledArr;
		AppendDisabledCapabilityMatches(Tokens, bRelaxedMatch, Settings, DisabledArr);
		if (DisabledArr.Num() > 0)
		{
			Output->SetStringField(TEXT("errorKind"), TEXT("disabled_only"));
			Output->SetStringField(TEXT("hint"),
				TEXT("No enabled matches; names below exist but are disabled in NexusLink settings. Enable in Editor Preferences and retry, or query=\"\" for the enabled catalog."));
			Output->SetArrayField(TEXT("disabledCapabilities"), DisabledArr);
		}
		else
		{
			FString Hint = TEXT("No matching capability. Try query=\"\" for the full catalog, or capabilityName=<exact name>.");
			if (QueryRaw.Equals(TEXT("get_asset"), ESearchCase::IgnoreCase))
			{
				Hint = TEXT("No get_asset aggregate tool; use get_asset_<type> (e.g. get_asset_blueprint) or search_asset for paths.");
			}
			else
			{
				FString GateHint;
				if (TryGatedPluginHint(QueryRaw.ToLower(), GateHint))
				{
					Hint = GateHint;
				}
			}
			Output->SetStringField(TEXT("errorKind"), TEXT("not_found"));
			Output->SetStringField(TEXT("hint"), Hint);
			Output->SetStringField(TEXT("_feedbackHint"),
				TEXT("submit_feedback(category=\"search_zero\")"));
		}

		static const TArray<FString> FallbackCaps = {
			TEXT("search_asset"),
			TEXT("get_asset_blueprint"),
			TEXT("get_asset_gameplay_ability"),
			TEXT("get_runtime_actor_property"),
		};
		TArray<TSharedPtr<FJsonValue>> TopArr;
		for (const FString& FbName : FallbackCaps)
		{
			const FCapRecord* R = FNexusCapabilityRegistry::Get().FindRecordByName(FbName);
			if (R && Settings->IsCapabilityEnabled(R->Def.Name))
			{
				TSharedPtr<FJsonObject> FbEntry = MakeShared<FJsonObject>();
				FbEntry->SetStringField(TEXT("name"),        R->Def.Name);
				FbEntry->SetStringField(TEXT("description"), R->Def.Description);
				TopArr.Add(MakeShared<FJsonValueObject>(FbEntry));
			}
		}
		if (TopArr.Num() > 0)
			Output->SetArrayField(TEXT("routingHints"), TopArr);

		FNexusFeedback::FFields F;
		F.Tool  = TEXT("search_capabilities");
		F.Query = QueryRaw;
		FNexusFeedback::RecordAuto(TEXT("search_zero"), F);
	}
	else if (bRelaxedMatch)
	{
		Output->SetStringField(TEXT("hint"),
			TEXT("Strict AND found no match; showing partial-match top results (relaxedMatch). Narrow query or pass capabilityName=<exact name>."));
		Output->SetStringField(TEXT("_feedbackHint"),
			TEXT("submit_feedback(category=\"search_zero\")"));
	}
	else if (!bFull)
	{
		const bool bWasTruncated = TotalBeforeTrunc > CapArr.Num();
		if (bWasTruncated)
		{
			Output->SetStringField(TEXT("hint"), FString::Printf(
				TEXT("Too many results (%d total, showing first %d). Narrow query to 1-2 precise words, or pass capabilityName=<exact name>."),
				TotalBeforeTrunc, CapArr.Num()));
		}
		else
		{
			Output->SetStringField(TEXT("hint"),
				TEXT("Multiple capabilities matched. Check whenToUse/relatedCapabilities, or pass capabilityName=<exact name> for full parameters."));
		}
		if (TotalBeforeTrunc > Settings->SearchOverflowThreshold)
		{
			FNexusFeedback::FFields F;
			F.Tool       = TEXT("search_capabilities");
			F.Query      = QueryRaw;
			F.MatchCount = TotalBeforeTrunc;
			FNexusFeedback::RecordAuto(TEXT("search_overflow"), F);
			Output->SetStringField(TEXT("_feedbackHint"),
				TEXT("submit_feedback(category=\"search_overflow\")"));
		}
	}

	Result.StructuredContent = Output;
	Result.OutputText = FNexusJsonUtils::SerializeCondensed(Output);
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolSearchCapabilities)
