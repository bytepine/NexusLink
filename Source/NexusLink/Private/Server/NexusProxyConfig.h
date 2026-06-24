// Copyright byteyang. All Rights Reserved.

#pragma once

#include "Dom/JsonObject.h"

/** 加载 Plugin/Resources/ProxyConfig.json，供 IDE 代理通过 nexus/proxy_config 拉取。 */
struct FNexusProxyConfig
{
	/** 构建完整配置 JSON（含 nexusLinkVersion 等运行时字段）。文件缺失时返回最小 fallback。 */
	static TSharedPtr<FJsonObject> BuildConfigObject();
};
