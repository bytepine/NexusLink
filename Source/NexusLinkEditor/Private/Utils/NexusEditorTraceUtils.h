// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_TRACE_READ

#include "Trace/StoreClient.h"
#include "TraceServices/Model/TimingProfiler.h"

// Utils 层：Editor。集中适配 Trace 计时器读取与 Store 客户端所有权。
class FNexusEditorTraceUtils final
{
public:
	FNexusEditorTraceUtils() = delete;

	static bool HasCpuTiming(const TraceServices::ITimingProfilerProvider& Timing)
	{
#if NX_UE_HAS_TRACE_TIMER_READER
		return Timing.GetTimerReader().GetTimerCount() > 0;
#else
		bool bHasCpuTiming = false;
		Timing.ReadTimers([&bHasCpuTiming](const TraceServices::ITimingProfilerTimerReader& Reader)
		{
			bHasCpuTiming = Reader.GetTimerCount() > 0;
		});
		return bHasCpuTiming;
#endif
	}

	static TUniquePtr<UE::Trace::FStoreClient> CreateStoreClient(const TCHAR* Host)
	{
#if NX_UE_HAS_TRACE_STORE_CREATE
		return UE::Trace::FStoreClient::Create(Host);
#else
		return TUniquePtr<UE::Trace::FStoreClient>(UE::Trace::FStoreClient::Connect(Host));
#endif
	}
};

#endif
