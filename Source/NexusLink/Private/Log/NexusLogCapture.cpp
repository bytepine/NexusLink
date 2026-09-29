// Copyright byteyang. All Rights Reserved.

#include "Log/NexusLogCapture.h"
#include "Algo/Reverse.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

FNexusLogCapture* FNexusLogCapture::Singleton = nullptr;

FNexusLogCapture::FNexusLogCapture()
{
	// 预分配环形缓冲区
	Buffer.SetNum(MaxEntries);
	WatchBuffer.SetNum(MaxWatchEntries);
	Singleton = this;
}

FNexusLogCapture::~FNexusLogCapture()
{
	Unregister();
	Singleton = nullptr;
}

FNexusLogCapture& FNexusLogCapture::Get()
{
	check(Singleton);
	return *Singleton;
}

TArray<FString> FNexusLogCapture::GetDefaultDiagnosticCategories()
{
	// 诊断向默认：业务 Print / 脚本 / 控制台镜像 / Ensure 相关 / 插件自身
	return {
		TEXT("LogTemp"),
		TEXT("LogBlueprintUserMessages"),
		TEXT("LogBlueprint"),
		TEXT("LogScript"),
		TEXT("LogScriptCore"),
		TEXT("LogOutputDevice"),
		TEXT("LogConsole"),
		TEXT("LogCore"),
		TEXT("LogStackWalk"),
		TEXT("LogNexusLink"),
		TEXT("LogUnLua"),
		TEXT("LogPIE"),
	};
}

void FNexusLogCapture::Register()
{
	if (!bRegistered && GLog)
	{
		GLog->AddOutputDevice(this);
		bRegistered = true;
	}
}

void FNexusLogCapture::Unregister()
{
	if (bRegistered && GLog)
	{
		GLog->RemoveOutputDevice(this);
		bRegistered = false;
	}
}

void FNexusLogCapture::SetCategoryWhitelist(const TArray<FString>& Categories)
{
	FScopeLock Lock(&Mutex);
	Whitelist.Reset(Categories.Num());
	ExactWhitelist.Reset();
	for (const FString& Cat : Categories)
	{
		if (!Cat.IsEmpty())
		{
			Whitelist.Add(Cat.ToUpper());
			ExactWhitelist.Add(FName(*Cat));
		}
	}
}

TArray<FString> FNexusLogCapture::GetCategoryWhitelist() const
{
	FScopeLock Lock(&Mutex);
	return Whitelist;
}

bool FNexusLogCapture::IsAllowed(const FName& Category) const
{
	if (Whitelist.Num() == 0) return true;
	if (ExactWhitelist.Contains(Category)) return true;
	const FString CatStr = Category.ToString();
	for (const FString& W : Whitelist)
	{
		if (CatStr.Contains(W, ESearchCase::IgnoreCase)) return true;
	}
	return false;
}

bool FNexusLogCapture::MatchesFilters(
	const FNexusLogEntry& E,
	const FNexusCompiledStringPattern& CategoryFilter,
	ELogVerbosity::Type VerbosityFilter,
	const TArray<FNexusCompiledStringPattern>& TextFilters)
{
	if (!CategoryFilter.Matches(E.Category))
		return false;
	if (VerbosityFilter != ELogVerbosity::All && E.Verbosity > VerbosityFilter)
		return false;
	if (TextFilters.Num() > 0)
	{
		bool bMatched = false;
		for (const FNexusCompiledStringPattern& TF : TextFilters)
		{
			if (TF.Matches(E.Message))
			{
				bMatched = true;
				break;
			}
		}
		if (!bMatched) return false;
	}
	return true;
}

void FNexusLogCapture::CopyFilledEntries(TArray<FNexusLogEntry>& Out) const
{
	FScopeLock Lock(&Mutex);
	const int32 Filled = FMath::Min(SlotWrites, MaxEntries);
	Out.Reset(Filled);
	if (Filled <= 0)
	{
		return;
	}
	const int32 StartIdx = (SlotWrites >= MaxEntries) ? WriteIndex : 0;
	for (int32 i = 0; i < Filled; ++i)
	{
		Out.Add(Buffer[(StartIdx + i) % MaxEntries]);
	}
}

void FNexusLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	// Fatal 级别触发时引擎即将崩溃，跳过写入避免死锁
	if (Verbosity == ELogVerbosity::Fatal) return;

	FScopeLock Lock(&Mutex);

	// 白名单过滤必须在锁内读取 Whitelist；Warning/Error 始终放行，避免收窄后漏诊。
	// watch 独立于白名单：未放行的行仍可能进旁路缓冲。
	const bool bSevere = Verbosity <= ELogVerbosity::Warning;
	if (bSevere || IsAllowed(Category))
	{
		WriteEntryLocked(Category.ToString(), Verbosity, V);
	}
	if (bWatchArmed)
	{
		TryWriteWatchLocked(Category.ToString(), Verbosity, V);
	}
}

void FNexusLogCapture::AppendEntry(const FString& Category, ELogVerbosity::Type Verbosity, const FString& Message)
{
	if (Message.IsEmpty() || Verbosity == ELogVerbosity::Fatal) return;

	FScopeLock Lock(&Mutex);
	WriteEntryLocked(Category, Verbosity, *Message);
	TryWriteWatchLocked(Category, Verbosity, *Message);
}

void FNexusLogCapture::WriteRingLocked(
	TArray<FNexusLogEntry>& Ring,
	int32 Capacity,
	int32& WriteIndex,
	int32& SlotWrites,
	int32& SequenceClock,
	int32& Dropped,
	bool bCountDrops,
	const FString& Category,
	ELogVerbosity::Type Verbosity,
	const TCHAR* Message)
{
	if (SlotWrites > 0)
	{
		const int32 LastIdx = (WriteIndex - 1 + Capacity) % Capacity;
		FNexusLogEntry& Last = Ring[LastIdx];
		if (Last.Verbosity == Verbosity && Last.Category == Category && Last.Message == Message)
		{
			Last.Repeat++;
			Last.Timestamp = FPlatformTime::Seconds();
			Last.WallTime = FDateTime::UtcNow();
			Last.Sequence = SequenceClock++;
			return;
		}
	}

	if (bCountDrops && SlotWrites >= Capacity)
	{
		Dropped++;
	}

	FNexusLogEntry& Entry = Ring[WriteIndex];
	Entry.Category = Category;
	Entry.Verbosity = Verbosity;
	Entry.Message = Message;
	Entry.Timestamp = FPlatformTime::Seconds();
	Entry.WallTime = FDateTime::UtcNow();
	Entry.Sequence = SequenceClock++;
	Entry.Repeat = 1;

	WriteIndex = (WriteIndex + 1) % Capacity;
	SlotWrites++;
}

void FNexusLogCapture::WriteEntryLocked(const FString& Category, ELogVerbosity::Type Verbosity, const TCHAR* Message)
{
	int32 IgnoredDrops = 0;
	WriteRingLocked(
		Buffer, MaxEntries, WriteIndex, SlotWrites, NextSequence,
		IgnoredDrops, false, Category, Verbosity, Message);
}

static bool NexusLogContainsI(const TCHAR* Haystack, const FString& Needle)
{
	return !Needle.IsEmpty() && Haystack && FCString::Stristr(Haystack, *Needle) != nullptr;
}

void FNexusLogCapture::TryWriteWatchLocked(const FString& Category, ELogVerbosity::Type Verbosity, const TCHAR* Message)
{
	if (!bWatchArmed || !Message) return;

	if (WatchSpec.MinVerbosity != ELogVerbosity::All && Verbosity > WatchSpec.MinVerbosity)
	{
		return;
	}
	if (WatchSpec.Categories.Num() > 0)
	{
		bool bCat = false;
		for (const FString& Cat : WatchSpec.Categories)
		{
			if (NexusLogContainsI(*Category, Cat))
			{
				bCat = true;
				break;
			}
		}
		if (!bCat) return;
	}
	if (WatchSpec.TextIncludes.Num() > 0)
	{
		bool bHit = false;
		for (const FString& Inc : WatchSpec.TextIncludes)
		{
			if (NexusLogContainsI(Message, Inc))
			{
				bHit = true;
				break;
			}
		}
		if (!bHit) return;
	}
	for (const FString& Exc : WatchSpec.TextExcludes)
	{
		if (NexusLogContainsI(Message, Exc)) return;
	}

	WriteRingLocked(
		WatchBuffer, MaxWatchEntries, WatchWriteIndex, WatchSlots, WatchSequence,
		WatchDropped, true, Category, Verbosity, Message);
}

int32 FNexusLogCapture::ArmWatch(const FNexusLogWatchSpec& Spec)
{
	FScopeLock Lock(&Mutex);
	WatchSpec = Spec;
	WatchSpec.Categories.RemoveAll([](const FString& S) { return S.IsEmpty(); });
	WatchSpec.TextIncludes.RemoveAll([](const FString& S) { return S.IsEmpty(); });
	WatchSpec.TextExcludes.RemoveAll([](const FString& S) { return S.IsEmpty(); });
	bWatchArmed = true;
	WatchWriteIndex = 0;
	WatchSlots = 0;
	WatchSequence = 0;
	WatchDropped = 0;
	ArmedLatestSequence = NextSequence > 0 ? NextSequence - 1 : -1;
	return ArmedLatestSequence;
}

void FNexusLogCapture::DisarmWatch()
{
	FScopeLock Lock(&Mutex);
	bWatchArmed = false;
	WatchSlots = 0;
	WatchWriteIndex = 0;
	WatchSequence = 0;
	WatchDropped = 0;
	ArmedLatestSequence = -1;
}

bool FNexusLogCapture::IsWatchArmed() const
{
	FScopeLock Lock(&Mutex);
	return bWatchArmed;
}

TArray<FNexusLogEntry> FNexusLogCapture::CopyWatchEntries(int32& OutDropped, int32& OutArmedLatestSequence) const
{
	FScopeLock Lock(&Mutex);
	OutDropped = WatchDropped;
	OutArmedLatestSequence = ArmedLatestSequence;

	TArray<FNexusLogEntry> Out;
	const int32 Filled = FMath::Min(WatchSlots, MaxWatchEntries);
	if (!bWatchArmed || Filled <= 0)
	{
		return Out;
	}
	const int32 StartIdx = (WatchSlots >= MaxWatchEntries) ? WatchWriteIndex : 0;
	Out.Reserve(Filled);
	for (int32 i = 0; i < Filled; ++i)
	{
		Out.Add(WatchBuffer[(StartIdx + i) % MaxWatchEntries]);
	}
	return Out;
}

int32 FNexusLogCapture::GetTotalWritten() const
{
	FScopeLock Lock(&Mutex);
	return NextSequence;
}

TArray<FNexusLogEntry> FNexusLogCapture::CollectSince(int32 SinceSequence) const
{
	FScopeLock Lock(&Mutex);

	TArray<FNexusLogEntry> Result;
	const int32 Filled = FMath::Min(SlotWrites, MaxEntries);
	if (Filled <= 0) return Result;

	const int32 StartIdx = (SlotWrites >= MaxEntries) ? WriteIndex : 0;
	Result.Reserve(Filled);

	for (int32 i = 0; i < Filled; ++i)
	{
		const FNexusLogEntry& E = Buffer[(StartIdx + i) % MaxEntries];
		if (E.Sequence > SinceSequence)
		{
			Result.Add(E);
		}
	}
	return Result;
}

int32 FNexusLogCapture::GetLatestSequence() const
{
	FScopeLock Lock(&Mutex);
	return NextSequence > 0 ? NextSequence - 1 : -1;
}

TArray<FNexusLogEntry> FNexusLogCapture::Query(
	int32 Offset,
	int32 Limit,
	const FString& CategoryFilter,
	ELogVerbosity::Type VerbosityFilter,
	const TArray<FString>& TextFilters,
	int32& OutTotalCount,
	int32 SinceSequence,
	bool bNewestFirst) const
{
	TArray<FNexusLogEntry> Snapshot;
	CopyFilledEntries(Snapshot);

	const FNexusCompiledStringPattern CompiledCategory(CategoryFilter);
	TArray<FNexusCompiledStringPattern> CompiledText;
	CompiledText.Reserve(TextFilters.Num());
	for (const FString& TF : TextFilters)
	{
		CompiledText.Emplace(TF);
	}

	TArray<int32> ValidIndices;
	ValidIndices.Reserve(Snapshot.Num());
	for (int32 i = 0; i < Snapshot.Num(); ++i)
	{
		const FNexusLogEntry& E = Snapshot[i];
		if (SinceSequence >= 0 && E.Sequence <= SinceSequence)
			continue;
		if (!MatchesFilters(E, CompiledCategory, VerbosityFilter, CompiledText))
			continue;
		ValidIndices.Add(i);
	}

	OutTotalCount = ValidIndices.Num();

	if (bNewestFirst)
	{
		Algo::Reverse(ValidIndices);
	}

	const int32 PageStart = FMath::Clamp(Offset, 0, OutTotalCount);
	const int32 PageEnd   = FMath::Min(PageStart + Limit, OutTotalCount);

	TArray<FNexusLogEntry> Result;
	Result.Reserve(PageEnd - PageStart);
	for (int32 i = PageStart; i < PageEnd; ++i)
	{
		Result.Add(MoveTemp(Snapshot[ValidIndices[i]]));
	}
	return Result;
}

void FNexusLogCapture::Summarize(
	const FString& CategoryFilter,
	ELogVerbosity::Type VerbosityFilter,
	const TArray<FString>& TextFilters,
	int32 SinceSequence,
	TArray<FNexusLogCategoryStat>& OutByCategory,
	TMap<ELogVerbosity::Type, int32>& OutByVerbosity) const
{
	TArray<FNexusLogEntry> Snapshot;
	CopyFilledEntries(Snapshot);

	const FNexusCompiledStringPattern CompiledCategory(CategoryFilter);
	TArray<FNexusCompiledStringPattern> CompiledText;
	CompiledText.Reserve(TextFilters.Num());
	for (const FString& TF : TextFilters)
	{
		CompiledText.Emplace(TF);
	}

	OutByCategory.Reset();
	OutByVerbosity.Reset();

	TMap<FString, FNexusLogCategoryStat> ByCat;

	for (const FNexusLogEntry& E : Snapshot)
	{
		if (SinceSequence >= 0 && E.Sequence <= SinceSequence)
			continue;
		if (!MatchesFilters(E, CompiledCategory, VerbosityFilter, CompiledText))
			continue;

		const int32 N = FMath::Max(1, E.Repeat);
		OutByVerbosity.FindOrAdd(E.Verbosity) += N;

		FNexusLogCategoryStat& Stat = ByCat.FindOrAdd(E.Category);
		Stat.Category = E.Category;
		Stat.Count += N;
		if (E.Verbosity == ELogVerbosity::Error) Stat.Errors += N;
		else if (E.Verbosity == ELogVerbosity::Warning) Stat.Warnings += N;
	}

	ByCat.GenerateValueArray(OutByCategory);
	OutByCategory.Sort([](const FNexusLogCategoryStat& A, const FNexusLogCategoryStat& B)
	{
		if (A.Count != B.Count) return A.Count > B.Count;
		return A.Category < B.Category;
	});
	if (OutByCategory.Num() > MaxSummaryCategories)
	{
		OutByCategory.SetNum(MaxSummaryCategories);
	}
}
