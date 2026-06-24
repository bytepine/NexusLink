// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * v1.8–v1.10 旧 Capability 名 → 当前规范名（与 Tests/_framework/legacy_map.py 同步）。
 * call_capability 在查找注册表前自动解析别名。
 */
class NEXUSLINK_API FNexusCapabilityLegacyNames
{
public:
	/** 若 Name 为旧名则返回规范名，否则返回 Trim 后的原名。 */
	static FString Resolve(const FString& Name);

	/** Name 是否为已知旧名（未改写的输入）。 */
	static bool IsLegacyName(const FString& Name);

	/** 旧名存在时返回规范名；否则返回空字符串。 */
	static FString GetCanonicalNameForLegacy(const FString& LegacyName);
};
