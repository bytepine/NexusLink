// Copyright byteyang. All Rights Reserved.

#include "Editor/NexusLogCapture.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformTime.h"

FNexusLogCapture* FNexusLogCapture::Singleton = nullptr;

FNexusLogCapture::FNexusLogCapture()
{
	// 预分配环形缓冲区
	Buffer.SetNum(MaxEntries);
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
	for (const FString& Cat : Categories)
	{
		if (!Cat.IsEmpty())
		{
			Whitelist.Add(Cat.ToUpper());
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
	// 白名单为空 = 全部通过
	if (Whitelist.Num() == 0) return true;
	const FString UpperCat = Category.ToString().ToUpper();
	for (const FString& W : Whitelist)
	{
		if (UpperCat.Contains(W)) return true;
	}
	return false;
}

void FNexusLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	// Fatal 级别触发时引擎即将崩溃，跳过写入避免死锁
	if (Verbosity == ELogVerbosity::Fatal) return;

	FScopeLock Lock(&Mutex);

	// 白名单过滤必须在锁内读取 Whitelist，避免与 SetCategoryWhitelist 的 TArray 重新分配产生数据竞态
	if (!IsAllowed(Category)) return;

	WriteEntryLocked(Category.ToString(), Verbosity, V);
}

void FNexusLogCapture::AppendEntry(const FString& Category, ELogVerbosity::Type Verbosity, const FString& Message)
{
	if (Message.IsEmpty() || Verbosity == ELogVerbosity::Fatal) return;

	FScopeLock Lock(&Mutex);
	WriteEntryLocked(Category, Verbosity, *Message);
}

void FNexusLogCapture::WriteEntryLocked(const FString& Category, ELogVerbosity::Type Verbosity, const TCHAR* Message)
{
	FNexusLogEntry& Entry = Buffer[WriteIndex % MaxEntries];
	Entry.Category   = Category;
	Entry.Verbosity  = Verbosity;
	Entry.Message    = Message;
	Entry.Timestamp  = FPlatformTime::Seconds();
	Entry.Sequence   = TotalWritten;

	WriteIndex = (WriteIndex + 1) % MaxEntries;
	TotalWritten++;
}

int32 FNexusLogCapture::GetTotalWritten() const
{
	FScopeLock Lock(&Mutex);
	return TotalWritten;
}

TArray<FNexusLogEntry> FNexusLogCapture::CollectSince(int32 SinceSequence) const
{
	FScopeLock Lock(&Mutex);

	TArray<FNexusLogEntry> Result;
	const int32 Filled = FMath::Min(TotalWritten, MaxEntries);
	if (Filled <= 0) return Result;

	const int32 StartIdx = (TotalWritten >= MaxEntries) ? WriteIndex : 0;
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

TArray<FNexusLogEntry> FNexusLogCapture::Query(
	int32 Offset,
	int32 Limit,
	const FString& CategoryFilter,
	ELogVerbosity::Type VerbosityFilter,
	const TArray<FString>& TextFilters,
	int32& OutTotalCount) const
{
	FScopeLock Lock(&Mutex);

	// 收集全量匹配条目（按时间升序）
	TArray<const FNexusLogEntry*> Valid;
	Valid.Reserve(FMath::Min(TotalWritten, MaxEntries));

	const int32 Filled = FMath::Min(TotalWritten, MaxEntries);
	// 缓冲区已满时最旧条目位于 WriteIndex，未满时从 0 开始
	const int32 StartIdx = (TotalWritten >= MaxEntries) ? WriteIndex : 0;

	for (int32 i = 0; i < Filled; ++i)
	{
		const FNexusLogEntry& E = Buffer[(StartIdx + i) % MaxEntries];

		if (!CategoryFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.Category, CategoryFilter))
			continue;
		if (VerbosityFilter != ELogVerbosity::All && E.Verbosity > VerbosityFilter)
			continue;
		if (TextFilters.Num() > 0)
		{
			bool bMatched = false;
			for (const FString& TF : TextFilters)
			{
				if (FNexusStringMatchUtils::Matches(E.Message, TF))
				{
					bMatched = true;
					break;
				}
			}
			if (!bMatched) continue;
		}

		Valid.Add(&E);
	}

	OutTotalCount = Valid.Num();

	// 分页切片
	const int32 PageStart = FMath::Clamp(Offset, 0, OutTotalCount);
	const int32 PageEnd   = FMath::Min(PageStart + Limit, OutTotalCount);

	TArray<FNexusLogEntry> Result;
	Result.Reserve(PageEnd - PageStart);
	for (int32 i = PageStart; i < PageEnd; ++i)
	{
		Result.Add(*Valid[i]);
	}
	return Result;
}


