// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Trace/NexusGetTraceCapability.h"

#if NX_UE_HAS_TRACE_READ

#include "Algo/BinarySearch.h"
#include "Capabilities/Trace/NexusTraceSession.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Diagnostics.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "Utils/NexusEditorTraceUtils.h"

static int32 ReadLimit(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, int32 DefaultValue, int32 MinValue, int32 MaxValue)
{
	if (!Args.IsValid() || !Args->HasField(Key))
	{
		return DefaultValue;
	}
	return FMath::Clamp(static_cast<int32>(Args->GetNumberField(Key)), MinValue, MaxValue);
}

static ETraceFrameType ParseFrameType(const TSharedPtr<FJsonObject>& Args)
{
	FString FrameType;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("FrameType"), FrameType);
	}
	if (FrameType.Equals(TEXT("Render"), ESearchCase::IgnoreCase))
	{
		return TraceFrameType_Rendering;
	}
	return TraceFrameType_Game;
}

static bool ReadTimeWindow(const TSharedPtr<FJsonObject>& Args, double& OutStart, double& OutEnd, bool& bOutHasWindow, FString& OutError)
{
	bOutHasWindow = false;
	const bool bHasStart = Args.IsValid() && Args->HasField(TEXT("StartTime"));
	const bool bHasEnd = Args.IsValid() && Args->HasField(TEXT("EndTime"));
	if (bHasStart != bHasEnd)
	{
		OutError = TEXT("StartTime and EndTime must be set together");
		return false;
	}
	if (!bHasStart)
	{
		return true;
	}
	OutStart = Args->GetNumberField(TEXT("StartTime"));
	OutEnd = Args->GetNumberField(TEXT("EndTime"));
	if (!(OutEnd > OutStart))
	{
		OutError = TEXT("EndTime must be greater than StartTime");
		return false;
	}
	bOutHasWindow = true;
	return true;
}

static void WriteSummary(const FNexusTraceAnalysisSession& Session, TSharedPtr<FJsonObject>& Detail)
{
	Detail->SetBoolField(TEXT("bAnalysisComplete"), Session.IsAnalysisComplete());

	FNexusTraceReadScope ReadScope(Session);
	Detail->SetNumberField(TEXT("DurationSeconds"), Session.GetDurationSeconds());

	const FNexusTraceFrameProvider& Frames = FNexusEditorTraceUtils::ReadFrames(Session);
	Detail->SetNumberField(TEXT("GameFrameCount"), static_cast<double>(Frames.GetFrameCount(TraceFrameType_Game)));
	Detail->SetNumberField(TEXT("RenderFrameCount"), static_cast<double>(Frames.GetFrameCount(TraceFrameType_Rendering)));

	bool bHasCpuTiming = false;
	if (const FNexusTraceTimingProvider* Timing = FNexusEditorTraceUtils::ReadTiming(Session))
	{
		bHasCpuTiming = FNexusEditorTraceUtils::HasCpuTiming(*Timing);
	}
	Detail->SetBoolField(TEXT("bHasCpuTiming"), bHasCpuTiming);

	FNexusEditorTraceUtils::WriteSessionInfo(Session, *Detail);
}

struct FNexusTraceFrameSample
{
	uint64 Index = 0;
	double StartTime = 0.0;
	double DurationSeconds = 0.0;
};

static void WriteFrames(const FNexusTraceAnalysisSession& Session, ETraceFrameType FrameType, int32 HitchLimit, TSharedPtr<FJsonObject>& Detail, FString& OutError)
{
	FNexusTraceReadScope ReadScope(Session);
	const FNexusTraceFrameProvider& Frames = FNexusEditorTraceUtils::ReadFrames(Session);
	const uint64 FrameCount64 = Frames.GetFrameCount(FrameType);
	if (FrameCount64 == 0)
	{
		OutError = TEXT("No frames in this trace. Record with the frame channel.");
		return;
	}
	if (FrameCount64 > 2000000)
	{
		OutError = TEXT("Trace has too many frames to summarize in-process");
		return;
	}

	const int32 FrameCount = static_cast<int32>(FrameCount64);
	TArray<double> Durations;
	TArray<FNexusTraceFrameSample> Hitches;
	Durations.Reserve(FrameCount);
	double TotalSeconds = 0.0;

	Frames.EnumerateFrames(FrameType, 0, FrameCount64, [&](const FNexusTraceFrame& Frame)
	{
		const double Duration = Frame.EndTime - Frame.StartTime;
		if (!FMath::IsFinite(Duration) || Duration < 0.0)
		{
			return;
		}
		Durations.Add(Duration);
		TotalSeconds += Duration;

		FNexusTraceFrameSample Sample;
		Sample.Index = Frame.Index;
		Sample.StartTime = Frame.StartTime;
		Sample.DurationSeconds = Duration;
		const int32 InsertAt = Algo::LowerBound(Hitches, Sample, [](const FNexusTraceFrameSample& A, const FNexusTraceFrameSample& B)
		{
			return A.DurationSeconds > B.DurationSeconds;
		});
		if (InsertAt < HitchLimit)
		{
			Hitches.Insert(Sample, InsertAt);
			if (Hitches.Num() > HitchLimit)
			{
				Hitches.SetNum(HitchLimit);
			}
		}
	});

	if (Durations.Num() == 0)
	{
		OutError = TEXT("No frames in this trace. Record with the frame channel.");
		return;
	}

	TArray<double> Sorted = Durations;
	Sorted.Sort();
	const int32 P95Index = FMath::Clamp(FMath::CeilToInt(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1);

	Detail->SetStringField(TEXT("FrameType"), FrameType == TraceFrameType_Rendering ? TEXT("Render") : TEXT("Game"));
	Detail->SetNumberField(TEXT("FrameCount"), Durations.Num());
	Detail->SetNumberField(TEXT("AverageSeconds"), TotalSeconds / Durations.Num());
	Detail->SetNumberField(TEXT("P95Seconds"), Sorted[P95Index]);
	Detail->SetNumberField(TEXT("MaxSeconds"), Sorted.Last());

	TArray<TSharedPtr<FJsonValue>> HitchValues;
	for (const FNexusTraceFrameSample& Hitch : Hitches)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("Index"), static_cast<double>(Hitch.Index));
		Obj->SetNumberField(TEXT("StartTime"), Hitch.StartTime);
		Obj->SetNumberField(TEXT("DurationSeconds"), Hitch.DurationSeconds);
		HitchValues.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Detail->SetArrayField(TEXT("Hitches"), HitchValues);
}

static bool FindWorstGameFrame(const FNexusTraceAnalysisSession& Session, double& OutStart, double& OutEnd)
{
	const FNexusTraceFrameProvider& Frames = FNexusEditorTraceUtils::ReadFrames(Session);
	const uint64 FrameCount = Frames.GetFrameCount(TraceFrameType_Game);
	bool bFound = false;
	double WorstDuration = -1.0;
	Frames.EnumerateFrames(TraceFrameType_Game, 0, FrameCount, [&](const FNexusTraceFrame& Frame)
	{
		const double Duration = Frame.EndTime - Frame.StartTime;
		// 尚未闭合的帧 EndTime 为 infinity，不能用来选默认分析窗口。
		if (!FMath::IsFinite(Duration) || Duration < 0.0) return;
		if (Duration > WorstDuration)
		{
			WorstDuration = Duration;
			OutStart = Frame.StartTime;
			OutEnd = Frame.EndTime;
			bFound = true;
		}
	});
	if (bFound && !(OutEnd > OutStart))
	{
		OutEnd = OutStart + 0.000001;
	}
	return bFound;
}

struct FNexusTraceTimerRow
{
	FString Name;
	uint64 Count = 0;
	double TotalInclusiveSeconds = 0.0;
	double MaxInclusiveSeconds = 0.0;
	double TotalExclusiveSeconds = 0.0;
	double MaxExclusiveSeconds = 0.0;
};

static void WriteTimers(const FNexusTraceAnalysisSession& Session, const TSharedPtr<FJsonObject>& Args, int32 TimerLimit, TSharedPtr<FJsonObject>& Detail, FString& OutError)
{
	double WindowStart = 0.0;
	double WindowEnd = 0.0;
	bool bHasWindow = false;
	if (!ReadTimeWindow(Args, WindowStart, WindowEnd, bHasWindow, OutError))
	{
		return;
	}

	FNexusTraceReadScope ReadScope(Session);
	if (!bHasWindow)
	{
		if (!FindWorstGameFrame(Session, WindowStart, WindowEnd))
		{
			OutError = TEXT("No game frames to choose a default window. Pass StartTime and EndTime, and record with the frame channel.");
			return;
		}
	}

	const FNexusTraceTimingProvider* Timing = FNexusEditorTraceUtils::ReadTiming(Session);
	if (Timing == nullptr)
	{
		OutError = TEXT("No CPU timing in this trace. Record with the cpu channel.");
		return;
	}

	FNexusTraceStatsTable* Table = FNexusEditorTraceUtils::CreateCpuAggregation(*Timing, WindowStart, WindowEnd);
	if (Table == nullptr)
	{
		OutError = TEXT("No CPU timing in this trace. Record with the cpu channel.");
		return;
	}

	TArray<FNexusTraceTimerRow> Rows;
	{
		TUniquePtr<FNexusTraceStatsReader> Reader(Table->CreateReader());
		while (Reader.IsValid() && Reader->IsValid())
		{
			const FNexusTraceAggregatedStats* Stats = Reader->GetCurrentRow();
			if (Stats != nullptr && Stats->Timer != nullptr && Stats->Timer->Name != nullptr && Stats->InstanceCount > 0)
			{
				FNexusTraceTimerRow& Row = Rows.AddDefaulted_GetRef();
				Row.Name = Stats->Timer->Name;
				Row.Count = Stats->InstanceCount;
				Row.TotalInclusiveSeconds = Stats->TotalInclusiveTime;
				Row.MaxInclusiveSeconds = Stats->MaxInclusiveTime;
				Row.TotalExclusiveSeconds = Stats->TotalExclusiveTime;
				Row.MaxExclusiveSeconds = Stats->MaxExclusiveTime;
			}
			Reader->NextRow();
		}
	}
	delete Table;

	if (Rows.Num() == 0)
	{
		OutError = TEXT("No CPU timing in this trace. Record with the cpu channel.");
		return;
	}

	Rows.Sort([](const FNexusTraceTimerRow& A, const FNexusTraceTimerRow& B)
	{
		return A.TotalInclusiveSeconds > B.TotalInclusiveSeconds;
	});
	if (Rows.Num() > TimerLimit)
	{
		Rows.SetNum(TimerLimit);
	}

	TArray<TSharedPtr<FJsonValue>> TimerValues;
	for (const FNexusTraceTimerRow& Row : Rows)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("Name"), Row.Name);
		Obj->SetNumberField(TEXT("Count"), static_cast<double>(Row.Count));
		Obj->SetNumberField(TEXT("TotalInclusiveSeconds"), Row.TotalInclusiveSeconds);
		Obj->SetNumberField(TEXT("MaxInclusiveSeconds"), Row.MaxInclusiveSeconds);
		Obj->SetNumberField(TEXT("TotalExclusiveSeconds"), Row.TotalExclusiveSeconds);
		Obj->SetNumberField(TEXT("MaxExclusiveSeconds"), Row.MaxExclusiveSeconds);
		TimerValues.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Detail->SetNumberField(TEXT("StartTime"), WindowStart);
	Detail->SetNumberField(TEXT("EndTime"), WindowEnd);
	Detail->SetArrayField(TEXT("Timers"), TimerValues);
}

static void WriteBookmarks(const FNexusTraceAnalysisSession& Session, const TSharedPtr<FJsonObject>& Args, int32 BookmarkLimit, TSharedPtr<FJsonObject>& Detail, FString& OutError)
{
	double WindowStart = 0.0;
	double WindowEnd = 0.0;
	bool bHasWindow = false;
	if (!ReadTimeWindow(Args, WindowStart, WindowEnd, bHasWindow, OutError))
	{
		return;
	}

	FNexusTraceReadScope ReadScope(Session);
	if (!bHasWindow)
	{
		WindowStart = 0.0;
		WindowEnd = FMath::Max(Session.GetDurationSeconds(), 1.0e12);
	}

	const FNexusTraceBookmarkProvider& Bookmarks = FNexusEditorTraceUtils::ReadBookmarks(Session);
	TArray<TSharedPtr<FJsonValue>> BookmarkValues;
	int32 Kept = 0;
	Bookmarks.EnumerateBookmarks(WindowStart, WindowEnd, [&](const FNexusTraceBookmark& Bookmark)
	{
		if (Kept >= BookmarkLimit)
		{
			return;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("Time"), Bookmark.Time);
		Obj->SetStringField(TEXT("Text"), Bookmark.Text != nullptr ? Bookmark.Text : TEXT(""));
		BookmarkValues.Add(MakeShared<FJsonValueObject>(Obj));
		++Kept;
	});
	Detail->SetArrayField(TEXT("Bookmarks"), BookmarkValues);
	Detail->SetNumberField(TEXT("Count"), BookmarkValues.Num());
}

void FNexusGetTraceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetTrace");
	Out.Description = TEXT("Read a stopped .utrace: frame stats, hitches, CPU timers, bookmarks.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Insights"), TEXT("utrace"), TEXT("hitch"), TEXT("profiling") };
	Out.RelatedCapabilities = { TEXT("ListTraces"), TEXT("GetOutputLog") };
	Out.WhenToUse = TEXT("Read one stopped .utrace");
}

TSharedPtr<FJsonObject> FNexusGetTraceCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("TracePath"), FNexusSchema::Str(TEXT("Path to a stopped .utrace file")))
		.Prop(TEXT("TraceId"), FNexusSchema::Int(TEXT("Store id if TracePath is omitted")))
		.Prop(TEXT("StartTime"), FNexusSchema::Num(TEXT("Window start in seconds")))
		.Prop(TEXT("EndTime"), FNexusSchema::Num(TEXT("Window end in seconds")))
		.Prop(TEXT("FrameType"), FNexusSchema::Enum(TEXT("Frame stats to summarize"), { TEXT("Game"), TEXT("Render") }, TEXT("Game")))
		.Prop(TEXT("HitchLimit"), FNexusSchema::Int(TEXT("Slowest frames to return"), 10, 1, 50))
		.Prop(TEXT("TimerLimit"), FNexusSchema::Int(TEXT("Top CPU timers to return"), 30, 1, 100))
		.Prop(TEXT("BookmarkLimit"), FNexusSchema::Int(TEXT("Bookmarks to return"), 50, 1, 200))
		.Build();
}

TArray<FString> FNexusGetTraceCapability::GetSectionNames() const
{
	return { TEXT("Summary"), TEXT("Frames"), TEXT("Timers"), TEXT("Bookmarks") };
}

TArray<FString> FNexusGetTraceCapability::GetDefaultSectionNames() const
{
	return { TEXT("Summary"), TEXT("Frames") };
}

bool FNexusGetTraceCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                            TSharedPtr<FJsonObject>&       OutEntry,
                                            void*&                         OutTargetOpaque,
                                            FString&                       OutError) const
{
	FString TracePath;
	const TSharedPtr<const FNexusTraceAnalysisSession> Session = FNexusTraceSessionCache::Open(Args, TracePath, OutError);
	if (!Session.IsValid())
	{
		OutEntry->SetStringField(TEXT("TracePath"), TracePath);
		return false;
	}

	OutEntry->SetStringField(TEXT("TracePath"), TracePath);
	OutEntry->SetBoolField(TEXT("bAnalysisComplete"), Session->IsAnalysisComplete());
	OutTargetOpaque = const_cast<FNexusTraceAnalysisSession*>(Session.Get());
	return true;
}

void FNexusGetTraceCapability::ExecuteSection(const FString&                 SectionName,
                                              const TSharedPtr<FJsonObject>& Args,
                                              void*                          TargetOpaque,
                                              TSharedPtr<FJsonObject>&       InOutDetail,
                                              FString&                       OutError) const
{
	const FNexusTraceAnalysisSession* Session = static_cast<const FNexusTraceAnalysisSession*>(TargetOpaque);
	if (Session == nullptr)
	{
		OutError = TEXT("Trace session is missing");
		return;
	}

	const bool bComplete = Session->IsAnalysisComplete();
	InOutDetail->SetBoolField(TEXT("bAnalysisComplete"), bComplete);
	if (!bComplete && SectionName != TEXT("Summary"))
	{
		OutError = TEXT("Analysis still running; call GetTrace again");
		return;
	}

	if (SectionName == TEXT("Summary"))
	{
		WriteSummary(*Session, InOutDetail);
		return;
	}
	if (SectionName == TEXT("Frames"))
	{
		WriteFrames(*Session, ParseFrameType(Args), ReadLimit(Args, TEXT("HitchLimit"), 10, 1, 50), InOutDetail, OutError);
		return;
	}
	if (SectionName == TEXT("Timers"))
	{
		WriteTimers(*Session, Args, ReadLimit(Args, TEXT("TimerLimit"), 30, 1, 100), InOutDetail, OutError);
		return;
	}
	if (SectionName == TEXT("Bookmarks"))
	{
		WriteBookmarks(*Session, Args, ReadLimit(Args, TEXT("BookmarkLimit"), 50, 1, 200), InOutDetail, OutError);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetTraceCapability)

#endif
