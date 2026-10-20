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

struct FNexusTraceListItem
{
	FString Path;
	int64 Size = 0;
	int64 UnixTime = 0;
	int32 TraceId = INDEX_NONE;
};

struct FNexusTraceStoreClient
{
	UE::Trace::FStoreClient* Client = nullptr;

	explicit FNexusTraceStoreClient(const TCHAR* Host)
	{
		Client = UE::Trace::FStoreClient::Connect(Host);
	}

	~FNexusTraceStoreClient()
	{
		if (Client)
		{
			delete Client;
		}
	}

	FNexusTraceStoreClient(const FNexusTraceStoreClient&) = delete;
	FNexusTraceStoreClient& operator=(const FNexusTraceStoreClient&) = delete;
};

struct FNexusTraceSessionSlot
{
	FString Path;
	int64 Size = -1;
	TSharedPtr<const TraceServices::IAnalysisSession> Session;
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

static FString Utf8ViewToString(FUtf8StringView View)
{
	if (View.IsEmpty())
	{
		return FString();
	}
	const FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(View.GetData()), View.Len());
	return FString(Convert.Length(), Convert.Get());
}

static void DisableHeavyTraceModules()
{
	static bool bTrimmed = false;
	if (bTrimmed)
	{
		return;
	}

	ITraceServicesModule& TraceModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	const TSharedPtr<TraceServices::IModuleService> Modules = TraceModule.GetModuleService();
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

static void AddTraceItem(TArray<FNexusTraceListItem>& Items, TSet<FString>& Seen, const FString& RawPath, int32 TraceId)
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

static FString StoreTracePath(UE::Trace::FStoreClient& Client, const UE::Trace::FStoreClient::FTraceInfo& Info)
{
	const FString Uri = Utf8ViewToString(Info.GetUri());
	if (!Uri.IsEmpty())
	{
		return Uri;
	}
	const UE::Trace::FStoreClient::FStatus* Status = Client.GetStatus();
	if (Status == nullptr)
	{
		return FString();
	}
	const FString StoreDir = Utf8ViewToString(Status->GetStoreDir());
	const FString Name = Utf8ViewToString(Info.GetName());
	if (StoreDir.IsEmpty() || Name.IsEmpty())
	{
		return FString();
	}
	return FPaths::SetExtension(FPaths::Combine(StoreDir, Name), TEXT("utrace"));
}

TSharedPtr<const TraceServices::IAnalysisSession> FNexusTraceSessionCache::Open(const FString& TracePath, FString& OutError)
{
	const FString Path = NormalizeTracePath(TracePath);
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Size < 0)
	{
		OutError = FString::Printf(TEXT("Trace file not found: %s"), *Path);
		return nullptr;
	}

	static FNexusTraceSessionSlot Slot;
	if (Slot.Session.IsValid() && Slot.Path == Path && Slot.Size == Size)
	{
		return Slot.Session;
	}
	if (Slot.Session.IsValid() && !Slot.Session->IsAnalysisComplete())
	{
		OutError = TEXT("Another trace is still analyzing; call GetTrace again before opening a different file");
		return nullptr;
	}

	DisableHeavyTraceModules();
	ITraceServicesModule& TraceModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	const TSharedPtr<TraceServices::IAnalysisService> Analysis = TraceModule.GetAnalysisService();
	if (!Analysis.IsValid())
	{
		OutError = TEXT("TraceServices analysis service is unavailable");
		return nullptr;
	}

	// StartAnalysis 自己起分析线程，并在打开时记下文件大小，不会跟着正在写的文件增长。
	TSharedPtr<const TraceServices::IAnalysisSession> Session = Analysis->StartAnalysis(*Path);
	if (!Session.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to open trace: %s"), *Path);
		return nullptr;
	}

	if (Slot.Session.IsValid())
	{
		Slot.Session->Stop(false);
		Slot.Session.Reset();
	}
	Slot.Path = Path;
	Slot.Size = Size;
	Slot.Session = Session;
	return Session;
}

bool FNexusTraceSessionCache::ResolveTracePath(const TSharedPtr<FJsonObject>& Args, FString& OutPath, FString& OutError)
{
	FString TracePath;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("TracePath"), TracePath);
	}
	if (!TracePath.IsEmpty())
	{
		OutPath = NormalizeTracePath(TracePath);
		return true;
	}

	double TraceIdNumber = 0.0;
	if (!Args.IsValid() || !Args->TryGetNumberField(TEXT("TraceId"), TraceIdNumber))
	{
		OutError = TEXT("TracePath or TraceId is required");
		return false;
	}

	FNexusTraceStoreClient Store(TEXT("127.0.0.1"));
	if (Store.Client == nullptr)
	{
		OutError = TEXT("Trace Store is not running, and TracePath was not given");
		return false;
	}
	const uint32 TraceId = static_cast<uint32>(TraceIdNumber);
	const UE::Trace::FStoreClient::FTraceInfo* Info = Store.Client->GetTraceInfoById(TraceId);
	if (Info == nullptr)
	{
		OutError = FString::Printf(TEXT("Trace id %u was not found in the local Trace Store"), TraceId);
		return false;
	}
	const FString StorePath = StoreTracePath(*Store.Client, *Info);
	if (StorePath.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Trace id %u has no file path"), TraceId);
		return false;
	}
	OutPath = NormalizeTracePath(StorePath);
	return true;
}

void FNexusTraceSessionCache::CollectRecentTraces(int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutTraces)
{
	TArray<FNexusTraceListItem> Items;
	TSet<FString> Seen;

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(FPaths::ProfilingDir() / TEXT("*.utrace")), true, false);
	for (const FString& FileName : FileNames)
	{
		AddTraceItem(Items, Seen, FPaths::ProfilingDir() / FileName, INDEX_NONE);
	}

	FNexusTraceStoreClient Store(TEXT("127.0.0.1"));
	if (Store.Client != nullptr)
	{
		const uint32 Count = Store.Client->GetTraceCount();
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			const UE::Trace::FStoreClient::FTraceInfo* Info = Store.Client->GetTraceInfo(Index);
			if (Info == nullptr)
			{
				continue;
			}
			AddTraceItem(Items, Seen, StoreTracePath(*Store.Client, *Info), static_cast<int32>(Info->GetId()));
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
			Obj->SetNumberField(TEXT("TraceId"), Item.TraceId);
		}
		OutTraces.Add(MakeShared<FJsonValueObject>(Obj));
	}
}

#endif
