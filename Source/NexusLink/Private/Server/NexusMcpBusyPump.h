// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** 只在阻塞循环中服务协议，不驱动世界、核心 Ticker 或业务工具。 */
class FNexusMcpBusyPump final
{
public:
	explicit FNexusMcpBusyPump(TFunction<void(float)> InService);
	~FNexusMcpBusyPump();
	static FString GetReason();
private:
	void Service(float DeltaTime);
	TFunction<void(float)> ServiceCallback;
	FDelegateHandle ModalTickHandle, PreModalHandle, PostModalHandle, ProgressHandle;
	uint64 NativeTimer = 0;
	double LastServiceAt = 0;
	bool bServicing = false;
	static int32 NativeModalDepth;
	static bool bInsideBlockedPump;
	static bool bInsideSlowTaskProgress;
};
