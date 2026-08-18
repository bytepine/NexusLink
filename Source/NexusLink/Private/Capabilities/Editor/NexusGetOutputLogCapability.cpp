// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusGetOutputLogCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor/NexusLogCapture.h"
#include "Utils/NexusResponseCompactorUtils.h"
#include "NexusMcpTool.h"

void FGetOutputLogCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_output_log");
	Out.Description = TEXT("Read UE console buffer. Diagnostic: preset=diagnose or newest+includeSummary; incremental via sinceSequence.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("offset"),         FNexusSchema::Int(TEXT("Pagination offset (along order direction)"), 0, 0))
		.Prop(TEXT("limit"),          FNexusSchema::Int(TEXT("Max items per page"), 100, 1, 500))
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
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("logs"), TEXT("console"), TEXT("messages"), TEXT("verbosity"), TEXT("warning"), TEXT("diagnose"), TEXT("summary") };
	Out.RelatedCapabilities = { TEXT("set_log_capture_filter"), TEXT("exec_command") };
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
		FString CategoryFilter;
		FString VerbosityStr = TEXT("log");
		TArray<FString> TextFilters;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("offset")))
				Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("limit"))), 1, 500);
				bLimitExplicit = true;
			}
			if (Arguments->HasField(TEXT("sinceSequence")))
				SinceSequence = static_cast<int32>(A.Num(TEXT("sinceSequence")));
			FString OrderStr;
			if (Arguments->TryGetStringField(TEXT("order"), OrderStr))
				bNewestFirst = !OrderStr.Equals(TEXT("oldest"), ESearchCase::IgnoreCase);
			FString PresetStr;
			if (Arguments->TryGetStringField(TEXT("preset"), PresetStr))
				bPresetDiagnose = PresetStr.Equals(TEXT("diagnose"), ESearchCase::IgnoreCase);
			if (Arguments->HasField(TEXT("includeSummary")))
				bIncludeSummary = A.Bool(TEXT("includeSummary"));
			if (Arguments->HasField(TEXT("summaryOnly")))
				bSummaryOnly = A.Bool(TEXT("summaryOnly"));
			Arguments->TryGetStringField(TEXT("categoryFilter"), CategoryFilter);
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

		ELogVerbosity::Type VerbosityFilter = ELogVerbosity::Log;
		if      (VerbosityStr == TEXT("fatal"))       VerbosityFilter = ELogVerbosity::Fatal;
		else if (VerbosityStr == TEXT("error"))        VerbosityFilter = ELogVerbosity::Error;
		else if (VerbosityStr == TEXT("warning"))      VerbosityFilter = ELogVerbosity::Warning;
		else if (VerbosityStr == TEXT("display"))      VerbosityFilter = ELogVerbosity::Display;
		else if (VerbosityStr == TEXT("log"))          VerbosityFilter = ELogVerbosity::Log;
		else if (VerbosityStr == TEXT("verbose"))      VerbosityFilter = ELogVerbosity::Verbose;
		else if (VerbosityStr == TEXT("veryverbose"))  VerbosityFilter = ELogVerbosity::VeryVerbose;
		else if (VerbosityStr == TEXT("all"))          VerbosityFilter = ELogVerbosity::All;

		auto VerbosityToString = [](ELogVerbosity::Type V) -> FString
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
		};

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
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			if (!E.Category.IsEmpty()) Item->SetStringField(TEXT("category"), E.Category);
			Item->SetStringField(TEXT("verbosity"), VerbosityToString(E.Verbosity));
			if (!E.Message.IsEmpty())  Item->SetStringField(TEXT("message"),  E.Message);
			Item->SetNumberField(TEXT("timestamp"), E.Timestamp);
			// ISO-8601 UTC，便于与用户「刚才」对齐；相对秒 timestamp 保留兼容
			if (E.WallTime.GetTicks() > 0)
			{
				Item->SetStringField(TEXT("time"), E.WallTime.ToIso8601());
			}
			Item->SetNumberField(TEXT("sequence"),  E.Sequence);
			LogArray.Add(MakeShared<FJsonValueObject>(Item));
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
				VerbObj->SetNumberField(VerbosityToString(Pair.Key), Pair.Value);
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
					EntryCompactor.AddForcedDefault(TEXT("verbosity"), VerbosityToString(VerbosityFilter));
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

#endif // WITH_EDITOR
