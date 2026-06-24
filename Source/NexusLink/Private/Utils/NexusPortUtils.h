// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Editor（私有头，仅 NexusLink 模块内部使用）
#include "CoreMinimal.h"

/**
 * 端口相关工具函数。
 */
class FNexusPortUtils
{
public:
	/** 检测指定端口是否已被占用（尝试绑定，成功则未占用并立即释放）。 */
	static bool IsPortInUse(int32 Port);

	/** 从 StartPort 开始向上查找第一个未被占用的端口；ExcludePorts 额外排除；返 -1 表示全部被占。 */
	static int32 FindAvailablePort(int32 StartPort, const TArray<int32>& ExcludePorts = TArray<int32>(), int32 MaxAttempts = 100);

#if WITH_EDITOR
	/** 打开 NexusLink 设置面板。 */
	static void OpenSettingsPanel();
#endif
};
