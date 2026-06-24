// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 管理本进程在临时目录中的 NexusLink 实例注册信息。
 *
 * 启动时向 {TempDir}/NexusLink/{PID}.json 写入端口信息，
 * 关闭时删除该文件，使 Rider 等客户端无需扫描端口即可发现全部活跃实例。
 *
 * 若进程崩溃未能执行 Unregister，下一次 Register 调用会自动清理残留文件。
 */
struct FNexusInstanceRegistry
{
	/** 注册当前实例，并清理已退出进程遗留的残留文件。 */
	static void Register(int32 McpPort, int32 WsPort, const FString& ProjectName, const FString& EngineVersion);

	/** 注销当前实例，删除本进程写入的注册文件。 */
	static void Unregister();

	/**
	 * 获取其他活跃实例已占用的端口列表（读取注册目录中所有有效 PID.json）。
	 * 用于端口选择时排除，避免 bind 探测与实际监听之间的 TOCTOU 竞态。
	 */
	static TArray<int32> GetClaimedPorts();
};
