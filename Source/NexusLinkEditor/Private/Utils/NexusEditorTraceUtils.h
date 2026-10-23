// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "HAL/ThreadSafeCounter.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_TRACE_READ

#include "Trace/StoreClient.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Diagnostics.h"
#include "TraceServices/Model/TimingProfiler.h"

#if NX_UE_HAS_TRACE_SERVICES_NAMESPACE
using FNexusTraceAnalysisSession = TraceServices::IAnalysisSession;
using FNexusTraceReadScope = TraceServices::FAnalysisSessionReadScope;
using FNexusTraceFrameProvider = TraceServices::IFrameProvider;
using FNexusTraceFrame = TraceServices::FFrame;
using FNexusTraceTimingProvider = TraceServices::ITimingProfilerProvider;
using FNexusTraceTimerReader = TraceServices::ITimingProfilerTimerReader;
using FNexusTraceAggregatedStats = TraceServices::FTimingProfilerAggregatedStats;
using FNexusTraceDiagnosticsProvider = TraceServices::IDiagnosticsProvider;
using FNexusTraceSessionInfo = TraceServices::FSessionInfo;
using FNexusTraceBookmarkProvider = TraceServices::IBookmarkProvider;
using FNexusTraceBookmark = TraceServices::FBookmark;
using FNexusTraceAnalysisService = TraceServices::IAnalysisService;
using FNexusTraceModuleService = TraceServices::IModuleService;
using FNexusTraceStatsTable = TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>;
using FNexusTraceStatsReader = TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>;
using FNexusTraceStoreClient = UE::Trace::FStoreClient;
using FNexusTraceInputStream = UE::Trace::IInDataStream;
#else
using FNexusTraceAnalysisSession = Trace::IAnalysisSession;
using FNexusTraceReadScope = Trace::FAnalysisSessionReadScope;
using FNexusTraceFrameProvider = Trace::IFrameProvider;
using FNexusTraceFrame = Trace::FFrame;
using FNexusTraceTimingProvider = Trace::ITimingProfilerProvider;
using FNexusTraceTimerReader = Trace::ITimingProfilerTimerReader;
using FNexusTraceAggregatedStats = Trace::FTimingProfilerAggregatedStats;
using FNexusTraceDiagnosticsProvider = Trace::IDiagnosticsProvider;
using FNexusTraceSessionInfo = Trace::FSessionInfo;
using FNexusTraceBookmarkProvider = Trace::IBookmarkProvider;
using FNexusTraceBookmark = Trace::FBookmark;
using FNexusTraceAnalysisService = Trace::IAnalysisService;
using FNexusTraceModuleService = Trace::IModuleService;
using FNexusTraceStatsTable = Trace::ITable<Trace::FTimingProfilerAggregatedStats>;
using FNexusTraceStatsReader = Trace::ITableReader<Trace::FTimingProfilerAggregatedStats>;
using FNexusTraceStoreClient = Trace::FStoreClient;
using FNexusTraceInputStream = Trace::IInDataStream;
#endif

/** Store 快照流：最多读打开时的大小，提前断流记录错误供查询线程读取。 */
class FNexusTraceSnapshotStream final : public FNexusTraceInputStream
{
public:
	/** 接管数据流所有权，ReadError 在分析完成前保持有效。 */
	FNexusTraceSnapshotStream(TUniquePtr<FNexusTraceInputStream>&& InSource, uint64 Size,
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> InReadError)
		: Source(MoveTemp(InSource)), Remaining(Size), ReadError(InReadError) {}

	/** 到快照边界返回 EOF；源流提前结束返回错误。 */
	virtual int32 Read(void* Data, uint32 Size) override
	{
		if (Remaining == 0 || Size == 0) return 0;
		const uint32 Requested = static_cast<uint32>(FMath::Min<uint64>(Remaining, FMath::Min<uint64>(Size, MAX_int32)));
		const int32 ReadSize = Source->Read(Data, Requested);
		if (ReadSize <= 0 || static_cast<uint32>(ReadSize) > Requested)
		{
			ReadError->Set(1);
			Remaining = 0;
			return -1;
		}
		Remaining -= ReadSize;
		return ReadSize;
	}

	/** 取消分析时关闭底层连接。 */
	virtual void Close() override { Source->Close(); }

private:
	TUniquePtr<FNexusTraceInputStream> Source;
	uint64 Remaining;
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> ReadError;
};

// Utils 层：Editor。集中适配 Trace 类型、Provider、聚合和 Store 接口。
class FNexusEditorTraceUtils final
{
public:
	FNexusEditorTraceUtils() = delete;

	/** 读取帧 Provider。 */
	static const FNexusTraceFrameProvider& ReadFrames(const FNexusTraceAnalysisSession& Session)
	{
#if NX_UE_HAS_TRACE_SERVICES_NAMESPACE
		return TraceServices::ReadFrameProvider(Session);
#else
		return Trace::ReadFrameProvider(Session);
#endif
	}

	/** 读取 CPU Provider。 */
	static const FNexusTraceTimingProvider* ReadTiming(const FNexusTraceAnalysisSession& Session)
	{
#if NX_UE_HAS_TRACE_SERVICES_NAMESPACE
		return TraceServices::ReadTimingProfilerProvider(Session);
#else
		return Trace::ReadTimingProfilerProvider(Session);
#endif
	}

	/** 读取 Bookmark Provider。 */
	static const FNexusTraceBookmarkProvider& ReadBookmarks(const FNexusTraceAnalysisSession& Session)
	{
#if NX_UE_HAS_TRACE_SERVICES_NAMESPACE
		return TraceServices::ReadBookmarkProvider(Session);
#else
		return Trace::ReadBookmarkProvider(Session);
#endif
	}

	/** 写入该版本存在的会话摘要字段。调用者持有 ReadScope。 */
	static void WriteSessionInfo(const FNexusTraceAnalysisSession& Session, FJsonObject& Detail)
	{
#if NX_UE_HAS_TRACE_SERVICES_NAMESPACE
		const FNexusTraceDiagnosticsProvider* Diagnostics = TraceServices::ReadDiagnosticsProvider(Session);
#else
		const FNexusTraceDiagnosticsProvider* Diagnostics = &Trace::ReadDiagnosticsProvider(Session);
#endif
		if (Diagnostics == nullptr || !Diagnostics->IsSessionInfoAvailable())
		{
			return;
		}
		const FNexusTraceSessionInfo& Info = Diagnostics->GetSessionInfo();
		if (!Info.Platform.IsEmpty()) Detail.SetStringField(TEXT("Platform"), Info.Platform);
		if (!Info.AppName.IsEmpty()) Detail.SetStringField(TEXT("AppName"), Info.AppName);
#if NX_UE_HAS_TRACE_PROJECT_NAME
		if (!Info.ProjectName.IsEmpty()) Detail.SetStringField(TEXT("ProjectName"), Info.ProjectName);
#endif
#if NX_UE_HAS_TRACE_BUILD_VERSION
		if (!Info.BuildVersion.IsEmpty()) Detail.SetStringField(TEXT("BuildVersion"), Info.BuildVersion);
#endif
	}

	/** 检查是否有计时器数据。 */
	static bool HasCpuTiming(const FNexusTraceTimingProvider& Timing)
	{
#if NX_UE_HAS_TRACE_TIMER_READER
		return Timing.GetTimerReader().GetTimerCount() > 0;
#else
		bool bHasCpuTiming = false;
		Timing.ReadTimers([&bHasCpuTiming](const FNexusTraceTimerReader& Reader)
		{
			bHasCpuTiming = Reader.GetTimerCount() > 0;
		});
		return bHasCpuTiming;
#endif
	}

	/** 创建 CPU 聚合表，所有版本均排除 GPU。返回表由调用者释放。 */
	static FNexusTraceStatsTable* CreateCpuAggregation(const FNexusTraceTimingProvider& Timing, double Start, double End)
	{
#if NX_UE_HAS_TRACE_AGGREGATION_PARAMS
		TraceServices::FCreateAggregationParams Params;
		Params.IntervalStart = Start;
		Params.IntervalEnd = End;
		Params.CpuThreadFilter = [](uint32) { return true; };
		Params.SortBy = TraceServices::FCreateAggregationParams::ESortBy::DontSort;
		return Timing.CreateAggregation(Params);
#elif NX_UE_HAS_TRACE_AGGREGATION_STRUCT
		TraceServices::FCreateAggreationParams Params{};
		Params.IntervalStart = Start;
		Params.IntervalEnd = End;
		Params.CpuThreadFilter = [](uint32) { return true; };
		Params.IncludeGpu = false;
		return Timing.CreateAggregation(Params);
#else
		return Timing.CreateAggregation(Start, End, [](uint32) { return true; }, false);
#endif
	}

	/** 在后台分析 Store 快照流，适配 UE4 缺少 TraceId 参数的重载。 */
	static TSharedPtr<const FNexusTraceAnalysisSession> StartStoreAnalysis(FNexusTraceAnalysisService& Analysis,
		uint32 TraceId, const TCHAR* Name, TUniquePtr<FNexusTraceInputStream>&& Stream)
	{
#if NX_UE_HAS_TRACE_SERVICES_NAMESPACE
		return Analysis.StartAnalysis(TraceId, Name, MoveTemp(Stream));
#else
		return Analysis.StartAnalysis(Name, MoveTemp(Stream));
#endif
	}

	/** 连接本机 Store；UE4 需要显式传默认端口 1989。 */
	static TUniquePtr<FNexusTraceStoreClient> CreateStoreClient(const TCHAR* Host, uint32 Port = 0)
	{
#if NX_UE_HAS_TRACE_STORE_CREATE
		return FNexusTraceStoreClient::Create(Host, Port);
#elif NX_UE_HAS_TRACE_STORE_DIRECTORY
		return TUniquePtr<FNexusTraceStoreClient>(FNexusTraceStoreClient::Connect(Host, Port));
#else
		return TUniquePtr<FNexusTraceStoreClient>(FNexusTraceStoreClient::Connect(Host, Port == 0 ? 1989 : Port));
#endif
	}

	/** 将 Store 的 UTF-8 名称或目录转换为 FString。 */
#if NX_UE_HAS_TRACE_UTF8_VIEW
	static FString StoreString(FUtf8StringView View)
#else
	static FString StoreString(FAnsiStringView View)
#endif
	{
		if (View.IsEmpty()) return FString();
		const FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(View.GetData()), View.Len());
		return FString(Convert.Length(), Convert.Get());
	}

	/** 获取真实路径；UE4 Store 不提供路径，返回空串。 */
	static FString StoreTracePath(FNexusTraceStoreClient& Client, const FNexusTraceStoreClient::FTraceInfo& Info)
	{
#if NX_UE_HAS_TRACE_STORE_URI
		const FString Uri = StoreString(Info.GetUri());
		if (!Uri.IsEmpty()) return Uri;
#endif
#if NX_UE_HAS_TRACE_STORE_DIRECTORY
		// GetStatus 会复用客户端响应缓冲，先复制 Info 中的名称。
		const FString Name = StoreString(Info.GetName());
		const FNexusTraceStoreClient::FStatus* Status = Client.GetStatus();
		if (Status == nullptr) return FString();
		const FString Directory = StoreString(Status->GetStoreDir());
		if (Directory.IsEmpty() || Name.IsEmpty()) return FString();
		return FPaths::SetExtension(FPaths::Combine(Directory, Name), TEXT("utrace"));
#else
		return FString();
#endif
	}
};

#endif
