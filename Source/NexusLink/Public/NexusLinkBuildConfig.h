// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * MCP 服务器是否编入本次构建。
 *
 * 正式包（Shipping）不带 MCP 服务器与 Capability 注册表，避免生产环境暴露调试端口；
 * Development / DebugGame（含独立 Game / DedicatedServer 目标）与 Editor 均保留。
 *
 * REGISTER_MCP_CAPABILITY / REGISTER_MCP_TOOL 用此开关代替原先的 WITH_EDITOR 判断，
 * 使独立 Game / DS 包（Type: Runtime 后）也能注册并托管 MCP。
 */
#ifndef NEXUSLINK_WITH_SERVER
	#define NEXUSLINK_WITH_SERVER (!UE_BUILD_SHIPPING)
#endif
