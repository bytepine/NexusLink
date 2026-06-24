// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Domain — §7.4 豁免：单 Capability 引用但合并了 2 处历史分散实现（>30 行）
#include "CoreMinimal.h"

#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
#endif

/**
 * 原始类型字符串 → FEdGraphPinType 的统一转换。
 *
 * 合并历史上分散的实现：
 *   - FNexusMcpToolManageBlueprintVariable::PinTypeFromString
 *   - FNexusMcpToolManageStructField::BuildPinTypeManage
 * 两者语义完全相同：先匹配内置基本类型 / 常用结构体，再按 UClass 名兜底。
 */
class NEXUSLINK_API FNexusPinTypeUtils
{
public:
#if WITH_EDITOR
	/**
	 * 把用户输入的类型字符串解析成 FEdGraphPinType。
	 *
	 * 支持的类型（大小写不敏感）：
	 *   - bool / int / float / string / name / text
	 *   - vector / rotator / transform（自动映射到对应 TBaseStructure）
	 *   - 其他：按 UClass 名查找（允许裸名、带 U 前缀、完整资产路径）
	 */
	static bool ParsePinType(const FString& TypeStr, FEdGraphPinType& OutPinType, FString& OutError);
#endif
};
