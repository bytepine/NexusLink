// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Trace/NexusTraceSession.h"

#if NX_UE_HAS_TRACE_READ

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Trace/StoreClient.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/ModuleService.h"
#include "Utils/NexusEditorTraceUtils.h"

struct FNexusTraceListItem
{
	FString Path;
	int64 Size = 0;
	int64 UnixTime = 0;
	int64 TraceId = INDEX_NONE;
};

struct FNexusTraceSessionSlot
{
	/** 模块卸载前结束分析，避免 Provider 或自定义流在工作线程仍使用时析构。 */
	~FNexusTraceSessionSlot()
	{
		if (Session.IsValid()) Session->Stop(true);
	}

	FString Path;
	int64 Size = -1;
	uint64 Timestamp = 0;
	uint32 StorePort = 0;
	int64 TraceId = INDEX_NONE;
	TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> ReadError;
	TSharedPtr<const FNexusTraceAnalysisSession> Session;
};

static FString NormalizeTracePath(const FString& InPath)
{
	FString Path = InPath;
	FPaths::NormalizeFilename(Path);
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
	else
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
	}
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
}

static void DisableHeavyTraceModules()
{
	static bool bTrimmed = false;
	if (bTrimmed)
	{
		return;
	}

	ITraceServicesModule& TraceModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	const TSharedPtr<FNexusTraceModuleService> Modules = TraceModule.GetModuleService();
	if (!Modules.IsValid())
	{
		return;
	}

	// 默认会建内存 / 加载 / 网络 / Cook 大表。第一期只查帧、CPU、Bookmark。
	static const FName HeavyModules[] = {
		FName(TEXT("TraceModule_Memory")),
		FName(TEXT("TraceModule_LoadTimeProfiler")),
		FName(TEXT("TraceModule_NetProfiler")),
		FName(TEXT("TraceModule_CookProfiler")),
	};
	for (const FName& ModuleName : HeavyModules)
	{
		Modules->SetModuleEnabled(ModuleName, false);
	}
	bTrimmed = true;
}

static int64 FileUnixTime(const FString& Path)
{
	const FFileStatData Stat = IFileManager::Get().GetStatData(*Path);
	if (!Stat.bIsValid)
	{
		return 0;
	}
	return Stat.ModificationTime.ToUnixTimestamp();
}

static void AddTraceItem(TArray<FNexusTraceListItem>& Items, TSet<FString>& Seen, const FString& RawPath, int64 TraceId)
{
	if (RawPath.IsEmpty())
	{
		return;
	}
	const FString Path = NormalizeTracePath(RawPath);
	if (Seen.Contains(Path))
	{
		return;
	}
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Size < 0)
	{
		return;
	}
	FNexusTraceListItem& Item = Items.AddDefaulted_GetRef();
	Item.Path = Path;
	Item.Size = Size;
	Item.UnixTime = FileUnixTime(Path);
	Item.TraceId = TraceId;
	Seen.Add(Path);
}

TSharedPtr<const FNexusTraceAnalysisSession> FNexusTraceSessionCache::Open(
	const TSharedPtr<FJsonObject>& Args, FString& OutPath, FString& OutError, uint32 StorePort)
{
	OutPath.Empty();
	FString TracePath;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("TracePath"), TracePath);
	int64 Size = -1;
	int64 TraceId = INDEX_NONE;
	uint64 Timestamp = 0;
	FString TraceName;
	TUniquePtr<FNexusTraceStoreClient> Store;
	if (!TracePath.IsEmpty())
	{
		OutPath = NormalizeTracePath(TracePath);
	}
	else
	{
		double TraceIdNumber = 0.0;
		if (!Args.IsValid() || !Args->TryGetNumberField(TEXT("TraceId"), TraceIdNumber))
		{
			OutError = TEXT("TracePath or TraceId is required");
			return nullptr;
		}
		if (!FMath::IsFinite(TraceIdNumber) || TraceIdNumber < 0 || TraceIdNumber > MAX_uint32
			|| TraceIdNumber != static_cast<double>(static_cast<uint32>(TraceIdNumber)))
		{
			OutError = TEXT("TraceId must be an unsigned 32-bit integer");
			return nullptr;
		}
		TraceId = static_cast<uint32>(TraceIdNumber);
		Store = FNexusEditorTraceUtils::CreateStoreClient(TEXT("127.0.0.1"), StorePort);
		if (!Store)
		{
			OutError = TEXT("Trace Store is not running, and TracePath was not given");
			return nullptr;
		}
		const FNexusTraceStoreClient::FTraceInfo* Info = Store->GetTraceInfoById(static_cast<uint32>(TraceId));
		if (Info == nullptr)
		{
			OutError = FString::Printf(TEXT("Trace id %u was not found in the local Trace Store"), static_cast<uint32>(TraceId));
			return nullptr;
		}
		// Store 的下一次请求会覆盖响应缓冲，先复制所有元数据。
		Size = static_cast<int64>(Info->GetSize());
		Timestamp = Info->GetTimestamp();
		TraceName = FNexusEditorTraceUtils::StoreString(Info->GetName());
		const FString StorePath = FNexusEditorTraceUtils::StoreTracePath(*Store, *Info);
		if (!StorePath.IsEmpty()) OutPath = NormalizeTracePath(StorePath);
		if (!OutPath.IsEmpty()) TraceId = INDEX_NONE;
	}
	if (!OutPath.IsEmpty())
	{
		Size = IFileManager::Get().FileSize(*OutPath);
		if (Size < 0)
		{
			OutError = FString::Printf(TEXT("Trace file not found: %s"), *OutPath);
			return nullptr;
		}
	}

	static FNexusTraceSessionSlot Slot;
	if (Slot.Session.IsValid() && Slot.Path == OutPath && Slot.Size == Size
		&& Slot.TraceId == TraceId && (TraceId == INDEX_NONE || (Slot.Timestamp == Timestamp && Slot.StorePort == StorePort)))
	{
		if (Slot.ReadError.IsValid() && Slot.ReadError->GetValue() != 0)
		{
			OutError = TEXT("Trace Store stream ended before the snapshot was fully read");
			return nullptr;
		}
		return Slot.Session;
	}
	if (Slot.Session.IsValid() && !Slot.Session->IsAnalysisComplete())
	{
		OutError = TEXT("Another trace is still analyzing; call GetTrace again before opening a different file");
		return nullptr;
	}

	DisableHeavyTraceModules();
	ITraceServicesModule& TraceModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	const TSharedPtr<FNexusTraceAnalysisService> Analysis = TraceModule.GetAnalysisService();
	if (!Analysis.IsValid())
	{
		OutError = TEXT("TraceServices analysis service is unavailable");
		return nullptr;
	}

	TSharedPtr<const FNexusTraceAnalysisSession> Session;
	TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> ReadError;
	if (!OutPath.IsEmpty())
	{
		Session = Analysis->StartAnalysis(*OutPath);
	}
	else
	{
		FNexusTraceStoreClient::FTraceData Data = Store->ReadTrace(static_cast<uint32>(TraceId));
		if (!Data)
		{
			OutError = TEXT("Failed to read trace from the local Trace Store");
			return nullptr;
		}
		ReadError = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
		TUniquePtr<FNexusTraceInputStream> Source(Data.Release());
		TUniquePtr<FNexusTraceInputStream> Snapshot(new FNexusTraceSnapshotStream(MoveTemp(Source), Size, ReadError.ToSharedRef()));
		Session = FNexusEditorTraceUtils::StartStoreAnalysis(*Analysis, static_cast<uint32>(TraceId), *TraceName, MoveTemp(Snapshot));
	}
	if (!Session.IsValid())
	{
		OutError = TEXT("Failed to open trace for analysis");
		return nullptr;
	}

	if (Slot.Session.IsValid()) Slot.Session->Stop(false);
	Slot.Path = OutPath;
	Slot.Size = Size;
	Slot.TraceId = TraceId;
	Slot.Timestamp = Timestamp;
	Slot.StorePort = StorePort;
	Slot.ReadError = ReadError;
	Slot.Session = Session;
	return Session;
}

void FNexusTraceSessionCache::CollectRecentTraces(int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutTraces, uint32 StorePort)
{
	TArray<FNexusTraceListItem> Items;
	TSet<FString> Seen;

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(FPaths::ProfilingDir() / TEXT("*.utrace")), true, false);
	for (const FString& FileName : FileNames)
	{
		AddTraceItem(Items, Seen, FPaths::ProfilingDir() / FileName, INDEX_NONE);
	}

	TUniquePtr<FNexusTraceStoreClient> Store = FNexusEditorTraceUtils::CreateStoreClient(TEXT("127.0.0.1"), StorePort);
	if (Store != nullptr)
	{
		const uint32 Count = Store->GetTraceCount();
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			const FNexusTraceStoreClient::FTraceInfo* Info = Store->GetTraceInfo(Index);
			if (Info == nullptr)
			{
				continue;
			}
			const uint32 TraceId = Info->GetId();
			const uint64 Size = Info->GetSize();
			const uint64 Timestamp = Info->GetTimestamp();
			const FString Path = FNexusEditorTraceUtils::StoreTracePath(*Store, *Info);
			if (Path.IsEmpty())
			{
				FNexusTraceListItem& Item = Items.AddDefaulted_GetRef();
				Item.TraceId = TraceId;
				Item.Size = static_cast<int64>(Size);
				Item.UnixTime = FDateTime(static_cast<int64>(Timestamp)).ToUnixTimestamp();
			}
			else
			{
				AddTraceItem(Items, Seen, Path, TraceId);
			}
		}
	}

	Items.Sort([](const FNexusTraceListItem& A, const FNexusTraceListItem& B)
	{
		return A.UnixTime > B.UnixTime;
	});
	if (Items.Num() > Limit)
	{
		Items.SetNum(Limit);
	}

	for (const FNexusTraceListItem& Item : Items)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("Path"), Item.Path);
		Obj->SetNumberField(TEXT("Size"), static_cast<double>(Item.Size));
		Obj->SetNumberField(TEXT("Timestamp"), static_cast<double>(Item.UnixTime));
		if (Item.TraceId != INDEX_NONE)
		{
			Obj->SetNumberField(TEXT("TraceId"), static_cast<double>(Item.TraceId));
		}
		OutTraces.Add(MakeShared<FJsonValueObject>(Obj));
	}
}

#endif
