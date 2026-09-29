// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Debug/NexusGetOutputLogCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Log/NexusLogCapture.h"
#include "Utils/NexusResponseCompactorUtils.h"
#include "NexusMcpTool.h"
#include "Algo/Reverse.h"

static ELogVerbosity::Type ParseLogVerbosity(const FString& VerbosityStr)
{
	if (VerbosityStr == TEXT("fatal"))       return ELogVerbosity::Fatal;
	if (VerbosityStr == TEXT("error"))       return ELogVerbosity::Error;
	if (VerbosityStr == TEXT("warning"))     return ELogVerbosity::Warning;
	if (VerbosityStr == TEXT("display"))     return ELogVerbosity::Display;
	if (VerbosityStr == TEXT("verbose"))     return ELogVerbosity::Verbose;
	if (VerbosityStr == TEXT("veryverbose")) return ELogVerbosity::VeryVerbose;
	if (VerbosityStr == TEXT("all"))         return ELogVerbosity::All;
	return ELogVerbosity::Log;
}

static const TCHAR* LogVerbosityLabel(ELogVerbosity::Type V)
{
	switch (V)
	{
	case ELogVerbosity::Fatal:       return TEXT("Fatal");
	case ELogVerbosity::Error:       return TEXT("Error");
	case ELogVerbosity::Warning:     return TEXT("Warning");
	case ELogVerbosity::Display:     return TEXT("Display");
	case ELogVerbosity::Log:         return TEXT("Log");
	case ELogVerbosity::Verbose:     return TEXT("Verbose");
	case ELogVerbosity::VeryVerbose: return TEXT("VeryVerbose");
	default:                         return TEXT("Unknown");
	}
}

static void ReadStringArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TArray<FString>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj.IsValid() || !Obj->TryGetArrayField(Key, Arr) || !Arr) return;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		if (!V.IsValid() || V->Type != EJson::String) continue;
		FString S = V->AsString().TrimStartAndEnd();
		if (!S.IsEmpty()) Out.Add(MoveTemp(S));
	}
}

static TSharedPtr<FJsonValue> LogEntryJson(const FNexusLogEntry& E)
{
	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	if (!E.Category.IsEmpty()) Item->SetStringField(TEXT("category"), E.Category);
	Item->SetStringField(TEXT("verbosity"), LogVerbosityLabel(E.Verbosity));
	if (!E.Message.IsEmpty())  Item->SetStringField(TEXT("message"), E.Message);
	Item->SetNumberField(TEXT("timestamp"), E.Timestamp);
	if (E.WallTime.GetTicks() > 0)
	{
		Item->SetStringField(TEXT("time"), E.WallTime.ToIso8601());
	}
	Item->SetNumberField(TEXT("sequence"), E.Sequence);
	if (E.Repeat > 1) Item->SetNumberField(TEXT("repeat"), E.Repeat);
	return MakeShared<FJsonValueObject>(Item);
}

void FGetOutputLogCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_output_log");
	Out.Description = TEXT("Console buffer. Arm watch, act, collectWatch=true. Or preset=diagnose.");
	TSharedPtr<FJsonObject> WatchSchema = FNexusSchema::Object()
		.Prop(TEXT("categories"), FNexusSchema::StrArr(TEXT("Category substrings; empty=all")))
		.Prop(TEXT("textIncludes"), FNexusSchema::StrArr(TEXT("Message substrings; any match")))
		.Prop(TEXT("textExcludes"), FNexusSchema::StrArr(TEXT("Drop messages containing these")))
		.Prop(TEXT("verbosity"), FNexusSchema::Enum(TEXT("Minimum verbosity level"),
			{ TEXT("error"), TEXT("warning"), TEXT("display"), TEXT("log"), TEXT("verbose"), TEXT("veryverbose"), TEXT("all") }, TEXT("log")))
		.Build();
	WatchSchema->SetStringField(TEXT("description"),
		TEXT("categories, textIncludes, textExcludes, verbosity; empty arms capture-all"));
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("offset"),         FNexusSchema::Int(TEXT("Pagination offset (along order direction)"), 0, 0))
		.Prop(TEXT("limit"),          FNexusSchema::Int(TEXT("Max items per page"), 100, 1, FNexusLogCapture::MaxWatchEntries))
		.Prop(TEXT("order"),          FNexusSchema::Enum(TEXT("Sort: newest=latest first (diagnostic default), oldest=ascending"),
			{ TEXT("newest"), TEXT("oldest") }, TEXT("newest")))
		.Prop(TEXT("sinceSequence"),  FNexusSchema::Int(TEXT("Return logs with Sequence greater than this (incremental; pass last latestSequence)"), -1, -1))
		.Prop(TEXT("preset"),         FNexusSchema::Enum(TEXT("Diagnostic preset: diagnose=newest+verbosity≥warning+includeSummary+limit≤50"),
			{ TEXT("none"), TEXT("diagnose") }, TEXT("none")))
		.Prop(TEXT("includeSummary"), FNexusSchema::Bool(TEXT("Attach summaryByCategory/summaryByVerbosity (full filtered set, not this page)"), true, false))
		.Prop(TEXT("summaryOnly"),    FNexusSchema::Bool(TEXT("Summary only; entries empty (still returns totalCount/latestSequence)"), true, false))
		.Prop(TEXT("categoryFilter"), FNexusSchema::Str(TEXT("Log category substring (case insensitive)")))
		.Prop(TEXT("verbosity"),      FNexusSchema::Enum(TEXT("Minimum verbosity level"),
			{ TEXT("error"), TEXT("warning"), TEXT("display"), TEXT("log"), TEXT("verbose"), TEXT("veryverbose"), TEXT("all") }, TEXT("log")))
		.Prop(TEXT("textFilter"),     FNexusSchema::Str(TEXT("Single text substring filter")))
		.Prop(TEXT("textFilters"),    FNexusSchema::StrArr(TEXT("Text filter (OR); overrides textFilter")))
		.Prop(TEXT("watch"),          WatchSchema.ToSharedRef())
		.Prop(TEXT("collectWatch"),   FNexusSchema::Bool(TEXT("Return the armed watch buffer"), true, false))
		.Prop(TEXT("disarm"),         FNexusSchema::Bool(TEXT("Disarm the watch after this call"), true, false))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("logs"), TEXT("console"), TEXT("messages"), TEXT("verbosity"), TEXT("warning"), TEXT("diagnose"), TEXT("summary"), TEXT("watch") };
	Out.RelatedCapabilities = { TEXT("set_log_capture_filter"), TEXT("exec_command") };
	Out.WhenToUse = TEXT("Arm watch{} (categories/textIncludes/textExcludes/verbosity), act, collectWatch=true.");
}

FCapabilityResult FGetOutputLogCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		int32 Offset = 0;
		int32 Limit  = 100;
		int32 SinceSequence = -1;
		bool  bNewestFirst = true;
		bool  bIncludeSummary = false;
		bool  bSummaryOnly = false;
		bool  bPresetDiagnose = false;
		bool  bVerbosityExplicit = false;
		bool  bLimitExplicit = false;
		bool  bOrderExplicit = false;
		bool  bCollectWatch = false;
		bool  bDisarm = false;
		const TSharedPtr<FJsonObject>* WatchObj = nullptr;
		FString CategoryFilter;
		FString VerbosityStr = TEXT("log");
		TArray<FString> TextFilters;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("offset")))
				Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("limit"))), 1, FNexusLogCapture::MaxWatchEntries);
				bLimitExplicit = true;
			}
			if (Arguments->HasField(TEXT("sinceSequence")))
				SinceSequence = static_cast<int32>(A.Num(TEXT("sinceSequence")));
			FString OrderStr;
			if (Arguments->TryGetStringField(TEXT("order"), OrderStr))
			{
				bNewestFirst = !OrderStr.Equals(TEXT("oldest"), ESearchCase::IgnoreCase);
				bOrderExplicit = true;
			}
			FString PresetStr;
			if (Arguments->TryGetStringField(TEXT("preset"), PresetStr))
				bPresetDiagnose = PresetStr.Equals(TEXT("diagnose"), ESearchCase::IgnoreCase);
			if (Arguments->HasField(TEXT("includeSummary")))
				bIncludeSummary = A.Bool(TEXT("includeSummary"));
			if (Arguments->HasField(TEXT("summaryOnly")))
				bSummaryOnly = A.Bool(TEXT("summaryOnly"));
			CategoryFilter = FNexusArgs(Arguments).Str(TEXT("categoryFilter"), CategoryFilter);
			FString TmpVerbosity;
			if (Arguments->TryGetStringField(TEXT("verbosity"), TmpVerbosity))
			{
				VerbosityStr = TmpVerbosity.ToLower();
				bVerbosityExplicit = true;
			}
			if (Arguments->HasField(TEXT("textFilters")))
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
				if (Arguments->TryGetArrayField(TEXT("textFilters"), ArrPtr) && ArrPtr)
					for (const TSharedPtr<FJsonValue>& V : *ArrPtr) { TextFilters.Add(V->AsString()); }
			}
			else
			{
				FString SingleFilter;
				if (Arguments->TryGetStringField(TEXT("textFilter"), SingleFilter) && !SingleFilter.IsEmpty())
					TextFilters.Add(SingleFilter);
			}
			if (Arguments->HasField(TEXT("collectWatch")))
				bCollectWatch = A.Bool(TEXT("collectWatch"));
			if (Arguments->HasField(TEXT("disarm")))
				bDisarm = A.Bool(TEXT("disarm"));
			Arguments->TryGetObjectField(TEXT("watch"), WatchObj);
		}

		const bool bHasWatch = WatchObj && WatchObj->IsValid();
		if (bHasWatch && bCollectWatch)
		{
			OutError = TEXT("Pass watch to arm, or collectWatch to read, not both");
			return;
		}
		if (bHasWatch)
		{
			FNexusLogWatchSpec Spec;
			ReadStringArray(*WatchObj, TEXT("categories"), Spec.Categories);
			ReadStringArray(*WatchObj, TEXT("textIncludes"), Spec.TextIncludes);
			ReadStringArray(*WatchObj, TEXT("textExcludes"), Spec.TextExcludes);
			FString WatchVerb;
			if ((*WatchObj)->TryGetStringField(TEXT("verbosity"), WatchVerb))
				Spec.MinVerbosity = ParseLogVerbosity(WatchVerb.ToLower());
			const int32 ArmedAt = FNexusLogCapture::Get().ArmWatch(Spec);

			TSharedPtr<FJsonObject> Armed = MakeShared<FJsonObject>();
			Armed->SetStringField(TEXT("watch"), TEXT("armed"));
			Armed->SetNumberField(TEXT("sinceSequence"), ArmedAt);
			Armed->SetNumberField(TEXT("latestSequence"), ArmedAt);
			Armed->SetNumberField(TEXT("totalCount"), 0);
			Armed->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
			Armed->SetStringField(TEXT("hint"),
				TEXT("After the action, call get_output_log with collectWatch=true"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Armed));
			return;
		}
		if (bDisarm && !bCollectWatch)
		{
			FNexusLogCapture::Get().DisarmWatch();
			TSharedPtr<FJsonObject> Disarmed = MakeShared<FJsonObject>();
			Disarmed->SetStringField(TEXT("watch"), TEXT("disarmed"));
			Disarmed->SetNumberField(TEXT("totalCount"), 0);
			Disarmed->SetNumberField(TEXT("latestSequence"), FNexusLogCapture::Get().GetLatestSequence());
			Disarmed->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
			OutEntries.Add(MakeShared<FJsonValueObject>(Disarmed));
			return;
		}
		if (bCollectWatch)
		{
			if (!FNexusLogCapture::Get().IsWatchArmed())
			{
				OutError = TEXT("No log watch armed; pass watch={} first");
				return;
			}
			if (!bOrderExplicit) bNewestFirst = false;
			if (!bLimitExplicit) Limit = FNexusLogCapture::MaxWatchEntries;

			int32 Dropped = 0;
			int32 ArmedAt = -1;
			TArray<FNexusLogEntry> Watched = FNexusLogCapture::Get().CopyWatchEntries(Dropped, ArmedAt);
			const int32 WatchedTotal = Watched.Num();
			if (bNewestFirst) Algo::Reverse(Watched);
			const int32 PageStart = FMath::Clamp(Offset, 0, WatchedTotal);
			const int32 PageEnd = FMath::Min(PageStart + Limit, WatchedTotal);
			TArray<TSharedPtr<FJsonValue>> WatchedJson;
			for (int32 i = PageStart; i < PageEnd; ++i)
				WatchedJson.Add(LogEntryJson(Watched[i]));
			if (bDisarm) FNexusLogCapture::Get().DisarmWatch();

			TSharedPtr<FJsonObject> Collected = MakeShared<FJsonObject>();
			Collected->SetStringField(TEXT("watch"), bDisarm ? TEXT("disarmed") : TEXT("armed"));
			Collected->SetNumberField(TEXT("sinceSequence"), ArmedAt);
			Collected->SetNumberField(TEXT("totalCount"), WatchedTotal);
			Collected->SetNumberField(TEXT("offset"), Offset);
			Collected->SetNumberField(TEXT("limit"), Limit);
			Collected->SetStringField(TEXT("order"), bNewestFirst ? TEXT("newest") : TEXT("oldest"));
			Collected->SetNumberField(TEXT("latestSequence"), FNexusLogCapture::Get().GetLatestSequence());
			if (Dropped > 0) Collected->SetNumberField(TEXT("dropped"), Dropped);
			Collected->SetArrayField(TEXT("entries"), WatchedJson);
			OutEntries.Add(MakeShared<FJsonValueObject>(Collected));
			return;
		}

		// diagnose 预设：最新 + ≥Warning + 摘要；未显式指定 limit 时压到 ≤50
		if (bPresetDiagnose)
		{
			bNewestFirst = true;
			bIncludeSummary = true;
			if (!bVerbosityExplicit) VerbosityStr = TEXT("warning");
			if (!bLimitExplicit) Limit = FMath::Min(Limit, 50);
		}
		if (bSummaryOnly)
		{
			bIncludeSummary = true;
		}

		const ELogVerbosity::Type VerbosityFilter = ParseLogVerbosity(VerbosityStr);

		int32 TotalCount = 0;
		TArray<FNexusLogEntry> Entries;
		TArray<FNexusLogCategoryStat> ByCat;
		TMap<ELogVerbosity::Type, int32> ByVerb;

		if (bIncludeSummary)
		{
			FNexusLogCapture::Get().Summarize(
				CategoryFilter, VerbosityFilter, TextFilters, SinceSequence, ByCat, ByVerb);
		}

		if (!bSummaryOnly)
		{
			Entries = FNexusLogCapture::Get().Query(
				Offset, Limit, CategoryFilter, VerbosityFilter, TextFilters, TotalCount,
				SinceSequence, bNewestFirst);
		}
		else
		{
			// 摘要模式下用 verbosity 合计作为 totalCount，避免再扫缓冲取页
			for (const TPair<ELogVerbosity::Type, int32>& Pair : ByVerb)
			{
				TotalCount += Pair.Value;
			}
		}
		const int32 LatestSequence = FNexusLogCapture::Get().GetLatestSequence();

		TArray<TSharedPtr<FJsonValue>> LogArray;
		for (const FNexusLogEntry& E : Entries)
		{
			LogArray.Add(LogEntryJson(E));
		}

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetNumberField(TEXT("totalCount"),     TotalCount);
		OutEntry->SetNumberField(TEXT("offset"),         Offset);
		OutEntry->SetNumberField(TEXT("limit"),          Limit);
		OutEntry->SetStringField(TEXT("order"),          bNewestFirst ? TEXT("newest") : TEXT("oldest"));
		OutEntry->SetNumberField(TEXT("latestSequence"), LatestSequence);
		if (bPresetDiagnose)
		{
			OutEntry->SetStringField(TEXT("preset"), TEXT("diagnose"));
		}
		OutEntry->SetArrayField(TEXT("entries"),         LogArray);

		if (bIncludeSummary)
		{
			TArray<TSharedPtr<FJsonValue>> CatArr;
			for (const FNexusLogCategoryStat& S : ByCat)
			{
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("category"), S.Category);
				Row->SetNumberField(TEXT("count"), S.Count);
				if (S.Errors > 0) Row->SetNumberField(TEXT("errors"), S.Errors);
				if (S.Warnings > 0) Row->SetNumberField(TEXT("warnings"), S.Warnings);
				CatArr.Add(MakeShared<FJsonValueObject>(Row));
			}
			OutEntry->SetArrayField(TEXT("summaryByCategory"), CatArr);

			TSharedPtr<FJsonObject> VerbObj = MakeShared<FJsonObject>();
			int32 ErrorCount = 0;
			int32 WarningCount = 0;
			for (const TPair<ELogVerbosity::Type, int32>& Pair : ByVerb)
			{
				VerbObj->SetNumberField(LogVerbosityLabel(Pair.Key), Pair.Value);
				if (Pair.Key == ELogVerbosity::Error) ErrorCount = Pair.Value;
				else if (Pair.Key == ELogVerbosity::Warning) WarningCount = Pair.Value;
			}
			OutEntry->SetObjectField(TEXT("summaryByVerbosity"), VerbObj);
			OutEntry->SetNumberField(TEXT("errorCount"), ErrorCount);
			OutEntry->SetNumberField(TEXT("warningCount"), WarningCount);
		}

		if (!CategoryFilter.IsEmpty() || VerbosityStr != TEXT("all"))
		{
			if (LogArray.Num() > 0)
			{
				FNexusResponseCompactorUtils EntryCompactor;
				// categoryFilter 是子串：只在本页实际 category 全员一致时 ForcedDefault 该值
				if (!CategoryFilter.IsEmpty())
				{
					EntryCompactor.AddForcedDefaultIfUnanimous(TEXT("category"), LogArray);
				}
				if (VerbosityStr != TEXT("all"))
				{
					// verbosity 是下限：Warning 页里 Error 条保留字段覆盖 defaults
					EntryCompactor.AddForcedDefault(TEXT("verbosity"), FString(LogVerbosityLabel(VerbosityFilter)));
				}
				EntryCompactor.CompactArray(LogArray);
				EntryCompactor.Emit(OutEntry, TEXT("entries"));
			}
		}

		const TArray<FString> Whitelist = FNexusLogCapture::Get().GetCategoryWhitelist();
		if (Whitelist.Num() == 0)
		{
			OutEntry->SetStringField(TEXT("captureFilter"), TEXT("all"));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> WlArr;
			for (const FString& Cat : Whitelist) WlArr.Add(MakeShared<FJsonValueString>(Cat));
			OutEntry->SetArrayField(TEXT("captureFilter"), WlArr);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetOutputLogCapability)
