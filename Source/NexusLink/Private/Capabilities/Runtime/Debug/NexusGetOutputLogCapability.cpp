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
	if (VerbosityStr == TEXT("Error"))       return ELogVerbosity::Error;
	if (VerbosityStr == TEXT("Warning"))     return ELogVerbosity::Warning;
	if (VerbosityStr == TEXT("Display"))     return ELogVerbosity::Display;
	if (VerbosityStr == TEXT("Verbose"))     return ELogVerbosity::Verbose;
	if (VerbosityStr == TEXT("Veryverbose")) return ELogVerbosity::VeryVerbose;
	if (VerbosityStr == TEXT("All"))         return ELogVerbosity::All;
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
	if (!E.Category.IsEmpty()) Item->SetStringField(TEXT("Category"), E.Category);
	Item->SetStringField(TEXT("Verbosity"), LogVerbosityLabel(E.Verbosity));
	if (!E.Message.IsEmpty())  Item->SetStringField(TEXT("Message"), E.Message);
	Item->SetNumberField(TEXT("Timestamp"), E.Timestamp);
	if (E.WallTime.GetTicks() > 0)
	{
		Item->SetStringField(TEXT("Time"), E.WallTime.ToIso8601());
	}
	Item->SetNumberField(TEXT("Sequence"), E.Sequence);
	if (E.Repeat > 1) Item->SetNumberField(TEXT("Repeat"), E.Repeat);
	return MakeShared<FJsonValueObject>(Item);
}

void FNexusGetOutputLogCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetOutputLog");
	Out.Description = TEXT("Console buffer. Arm watch, act, bCollectWatch=true. Or Preset=diagnose.");
	TSharedPtr<FJsonObject> WatchSchema = FNexusSchema::Object()
		.Prop(TEXT("Categories"), FNexusSchema::StrArr(TEXT("Category substrings; empty=all")))
		.Prop(TEXT("TextIncludes"), FNexusSchema::StrArr(TEXT("Message substrings; any match")))
		.Prop(TEXT("TextExcludes"), FNexusSchema::StrArr(TEXT("Drop messages containing these")))
		.Prop(TEXT("Verbosity"), FNexusSchema::Enum(TEXT("Minimum verbosity level"),
			{ TEXT("Error"), TEXT("Warning"), TEXT("Display"), TEXT("Log"), TEXT("Verbose"), TEXT("Veryverbose"), TEXT("All") }, TEXT("Log")))
		.Build();
	WatchSchema->SetStringField(TEXT("description"),
		TEXT("categories, TextIncludes, TextExcludes, verbosity; empty arms capture-all"));
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Offset"),         FNexusSchema::Int(TEXT("Pagination offset (along order direction)"), 0, 0))
		.Prop(TEXT("Limit"),          FNexusSchema::Int(TEXT("Max items per page"), 100, 1, FNexusLogCapture::MaxWatchEntries))
		.Prop(TEXT("Order"),          FNexusSchema::Enum(TEXT("Sort: Newest=latest first (diagnostic default), Oldest=ascending"),
			{ TEXT("Newest"), TEXT("Oldest") }, TEXT("Newest")))
		.Prop(TEXT("SinceSequence"),  FNexusSchema::Int(TEXT("Return logs with Sequence greater than this (incremental; pass last LatestSequence)"), -1, -1))
		.Prop(TEXT("Preset"),         FNexusSchema::Enum(TEXT("Diagnostic preset: Diagnose=newest+verbosity≥warning+bIncludeSummary+limit≤50"),
			{ TEXT("None"), TEXT("Diagnose") }, TEXT("None")))
		.Prop(TEXT("bIncludeSummary"), FNexusSchema::Bool(TEXT("Attach SummaryByCategory/SummaryByVerbosity (full filtered set, not this page)"), true, false))
		.Prop(TEXT("bSummaryOnly"),    FNexusSchema::Bool(TEXT("Summary only; entries empty (still returns TotalCount/LatestSequence)"), true, false))
		.Prop(TEXT("CategoryFilter"), FNexusSchema::Str(TEXT("Log category substring (case insensitive)")))
		.Prop(TEXT("Verbosity"),      FNexusSchema::Enum(TEXT("Minimum verbosity level"),
			{ TEXT("Error"), TEXT("Warning"), TEXT("Display"), TEXT("Log"), TEXT("Verbose"), TEXT("Veryverbose"), TEXT("All") }, TEXT("Log")))
		.Prop(TEXT("TextFilter"),     FNexusSchema::Str(TEXT("Single text substring filter")))
		.Prop(TEXT("TextFilters"),    FNexusSchema::StrArr(TEXT("Text filter (OR); overrides TextFilter")))
		.Prop(TEXT("Watch"),          WatchSchema.ToSharedRef())
		.Prop(TEXT("bCollectWatch"),   FNexusSchema::Bool(TEXT("Return the armed watch buffer"), true, false))
		.Prop(TEXT("bDisarm"),         FNexusSchema::Bool(TEXT("Disarm the watch after this call"), true, false))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("logs"), TEXT("console"), TEXT("messages"), TEXT("Verbosity"), TEXT("Warning"), TEXT("Diagnose"), TEXT("Summary"), TEXT("Watch") };
	Out.RelatedCapabilities = { TEXT("SetLogCaptureFilter"), TEXT("ExecCommand") };
	Out.WhenToUse = TEXT("Arm watch{} (categories/TextIncludes/TextExcludes/verbosity), act, bCollectWatch=true.");
}

FCapabilityResult FNexusGetOutputLogCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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
		FString VerbosityStr = TEXT("Log");
		TArray<FString> TextFilters;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("Offset")))
				Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
			if (Arguments->HasField(TEXT("Limit")))
			{
				Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, FNexusLogCapture::MaxWatchEntries);
				bLimitExplicit = true;
			}
			if (Arguments->HasField(TEXT("SinceSequence")))
				SinceSequence = static_cast<int32>(A.Num(TEXT("SinceSequence")));
			FString OrderStr;
			if (Arguments->TryGetStringField(TEXT("Order"), OrderStr))
			{
				bNewestFirst = !OrderStr.Equals(TEXT("Oldest"), ESearchCase::IgnoreCase);
				bOrderExplicit = true;
			}
			FString PresetStr;
			if (Arguments->TryGetStringField(TEXT("Preset"), PresetStr))
				bPresetDiagnose = PresetStr.Equals(TEXT("Diagnose"), ESearchCase::IgnoreCase);
			if (Arguments->HasField(TEXT("bIncludeSummary")))
				bIncludeSummary = A.Bool(TEXT("bIncludeSummary"));
			if (Arguments->HasField(TEXT("bSummaryOnly")))
				bSummaryOnly = A.Bool(TEXT("bSummaryOnly"));
			CategoryFilter = FNexusArgs(Arguments).Str(TEXT("CategoryFilter"), CategoryFilter);
			FString TmpVerbosity;
			if (Arguments->TryGetStringField(TEXT("Verbosity"), TmpVerbosity))
			{
				VerbosityStr = TmpVerbosity.ToLower();
				bVerbosityExplicit = true;
			}
			if (Arguments->HasField(TEXT("TextFilters")))
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
				if (Arguments->TryGetArrayField(TEXT("TextFilters"), ArrPtr) && ArrPtr)
					for (const TSharedPtr<FJsonValue>& V : *ArrPtr) { TextFilters.Add(V->AsString()); }
			}
			else
			{
				FString SingleFilter;
				if (Arguments->TryGetStringField(TEXT("TextFilter"), SingleFilter) && !SingleFilter.IsEmpty())
					TextFilters.Add(SingleFilter);
			}
			if (Arguments->HasField(TEXT("bCollectWatch")))
				bCollectWatch = A.Bool(TEXT("bCollectWatch"));
			if (Arguments->HasField(TEXT("bDisarm")))
				bDisarm = A.Bool(TEXT("bDisarm"));
			Arguments->TryGetObjectField(TEXT("Watch"), WatchObj);
		}

		const bool bHasWatch = WatchObj && WatchObj->IsValid();
		if (bHasWatch && bCollectWatch)
		{
			OutError = TEXT("Pass watch to arm, or bCollectWatch to read, not both");
			return;
		}
		if (bHasWatch)
		{
			FNexusLogWatchSpec Spec;
			ReadStringArray(*WatchObj, TEXT("Categories"), Spec.Categories);
			ReadStringArray(*WatchObj, TEXT("TextIncludes"), Spec.TextIncludes);
			ReadStringArray(*WatchObj, TEXT("TextExcludes"), Spec.TextExcludes);
			FString WatchVerb;
			if ((*WatchObj)->TryGetStringField(TEXT("Verbosity"), WatchVerb))
				Spec.MinVerbosity = ParseLogVerbosity(WatchVerb.ToLower());
			const int32 ArmedAt = FNexusLogCapture::Get().ArmWatch(Spec);

			TSharedPtr<FJsonObject> Armed = MakeShared<FJsonObject>();
			Armed->SetStringField(TEXT("Watch"), TEXT("armed"));
			Armed->SetNumberField(TEXT("SinceSequence"), ArmedAt);
			Armed->SetNumberField(TEXT("LatestSequence"), ArmedAt);
			Armed->SetNumberField(TEXT("TotalCount"), 0);
			Armed->SetArrayField(TEXT("Entries"), TArray<TSharedPtr<FJsonValue>>());
			Armed->SetStringField(TEXT("Hint"),
				TEXT("After the action, call GetOutputLog with bCollectWatch=true"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Armed));
			return;
		}
		if (bDisarm && !bCollectWatch)
		{
			FNexusLogCapture::Get().DisarmWatch();
			TSharedPtr<FJsonObject> Disarmed = MakeShared<FJsonObject>();
			Disarmed->SetStringField(TEXT("Watch"), TEXT("disarmed"));
			Disarmed->SetNumberField(TEXT("TotalCount"), 0);
			Disarmed->SetNumberField(TEXT("LatestSequence"), FNexusLogCapture::Get().GetLatestSequence());
			Disarmed->SetArrayField(TEXT("Entries"), TArray<TSharedPtr<FJsonValue>>());
			OutEntries.Add(MakeShared<FJsonValueObject>(Disarmed));
			return;
		}
		if (bCollectWatch)
		{
			if (!FNexusLogCapture::Get().IsWatchArmed())
			{
				OutError = TEXT("No log watch armed; pass Watch={} first");
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
			Collected->SetStringField(TEXT("Watch"), bDisarm ? TEXT("disarmed") : TEXT("armed"));
			Collected->SetNumberField(TEXT("SinceSequence"), ArmedAt);
			Collected->SetNumberField(TEXT("TotalCount"), WatchedTotal);
			Collected->SetNumberField(TEXT("Offset"), Offset);
			Collected->SetNumberField(TEXT("Limit"), Limit);
			Collected->SetStringField(TEXT("Order"), bNewestFirst ? TEXT("Newest") : TEXT("Oldest"));
			Collected->SetNumberField(TEXT("LatestSequence"), FNexusLogCapture::Get().GetLatestSequence());
			if (Dropped > 0) Collected->SetNumberField(TEXT("Dropped"), Dropped);
			Collected->SetArrayField(TEXT("Entries"), WatchedJson);
			OutEntries.Add(MakeShared<FJsonValueObject>(Collected));
			return;
		}

		// diagnose 预设：最新 + ≥Warning + 摘要；未显式指定 limit 时压到 ≤50
		if (bPresetDiagnose)
		{
			bNewestFirst = true;
			bIncludeSummary = true;
			if (!bVerbosityExplicit) VerbosityStr = TEXT("Warning");
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
			// 摘要模式下用 verbosity 合计作为 TotalCount，避免再扫缓冲取页
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
		OutEntry->SetNumberField(TEXT("TotalCount"),     TotalCount);
		OutEntry->SetNumberField(TEXT("Offset"),         Offset);
		OutEntry->SetNumberField(TEXT("Limit"),          Limit);
		OutEntry->SetStringField(TEXT("Order"),          bNewestFirst ? TEXT("Newest") : TEXT("Oldest"));
		OutEntry->SetNumberField(TEXT("LatestSequence"), LatestSequence);
		if (bPresetDiagnose)
		{
			OutEntry->SetStringField(TEXT("Preset"), TEXT("Diagnose"));
		}
		OutEntry->SetArrayField(TEXT("Entries"),         LogArray);

		if (bIncludeSummary)
		{
			TArray<TSharedPtr<FJsonValue>> CatArr;
			for (const FNexusLogCategoryStat& S : ByCat)
			{
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("Category"), S.Category);
				Row->SetNumberField(TEXT("Count"), S.Count);
				if (S.Errors > 0) Row->SetNumberField(TEXT("Errors"), S.Errors);
				if (S.Warnings > 0) Row->SetNumberField(TEXT("Warnings"), S.Warnings);
				CatArr.Add(MakeShared<FJsonValueObject>(Row));
			}
			OutEntry->SetArrayField(TEXT("SummaryByCategory"), CatArr);

			TSharedPtr<FJsonObject> VerbObj = MakeShared<FJsonObject>();
			int32 ErrorCount = 0;
			int32 WarningCount = 0;
			for (const TPair<ELogVerbosity::Type, int32>& Pair : ByVerb)
			{
				VerbObj->SetNumberField(LogVerbosityLabel(Pair.Key), Pair.Value);
				if (Pair.Key == ELogVerbosity::Error) ErrorCount = Pair.Value;
				else if (Pair.Key == ELogVerbosity::Warning) WarningCount = Pair.Value;
			}
			OutEntry->SetObjectField(TEXT("SummaryByVerbosity"), VerbObj);
			OutEntry->SetNumberField(TEXT("ErrorCount"), ErrorCount);
			OutEntry->SetNumberField(TEXT("WarningCount"), WarningCount);
		}

		if (!CategoryFilter.IsEmpty() || VerbosityStr != TEXT("All"))
		{
			if (LogArray.Num() > 0)
			{
				FNexusResponseCompactorUtils EntryCompactor;
				// CategoryFilter 是子串：只在本页实际 category 全员一致时 ForcedDefault 该值
				if (!CategoryFilter.IsEmpty())
				{
					EntryCompactor.AddForcedDefaultIfUnanimous(TEXT("Category"), LogArray);
				}
				if (VerbosityStr != TEXT("All"))
				{
					// verbosity 是下限：Warning 页里 Error 条保留字段覆盖 defaults
					EntryCompactor.AddForcedDefault(TEXT("Verbosity"), FString(LogVerbosityLabel(VerbosityFilter)));
				}
				EntryCompactor.CompactArray(LogArray);
				EntryCompactor.Emit(OutEntry, TEXT("Entries"));
			}
		}

		const TArray<FString> Whitelist = FNexusLogCapture::Get().GetCategoryWhitelist();
		if (Whitelist.Num() == 0)
		{
			OutEntry->SetStringField(TEXT("CaptureFilter"), TEXT("All"));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> WlArr;
			for (const FString& Cat : Whitelist) WlArr.Add(MakeShared<FJsonValueString>(Cat));
			OutEntry->SetArrayField(TEXT("CaptureFilter"), WlArr);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetOutputLogCapability)
