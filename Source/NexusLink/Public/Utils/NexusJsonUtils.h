// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Common
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * JSON 对象字段安全读取与通用入参解析工具。
 * 供所有 Capability / Tool 统一处理 Arguments 字段提取，避免重复的 HasField + GetXxxField 样板。
 */
class NEXUSLINK_API FNexusJsonUtils final
{
public:
	FNexusJsonUtils() = delete;

	/** 安全取字符串字段，键不存在时返回 Default。 */
	static FString GetStringSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& Default = FString());

	/** 安全取整数字段，键不存在时返回 Default；结果 Clamp 到 [ClampMin, ClampMax]。 */
	static int32 GetIntSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key,
	                        int32 Default = 0, int32 ClampMin = 0, int32 ClampMax = INT32_MAX);

	/** 安全取浮点字段，键不存在时返回 Default。 */
	static float GetFloatSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float Default = 0.f);

	/** 安全取布尔字段，键不存在时返回 Default。 */
	static bool GetBoolSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Default = false);

	/** 取字符串数组字段，仅保留非空字符串元素；键不存在时返回空数组。 */
	static TArray<FString> GetStringArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key);

	/**
	 * sections 数组中是否含 "all"（大小写不敏感）。
	 * 供 redundant_call 去重逻辑及 MultiSectionCapability 路由判断复用。
	 */
	static bool HasSectionAll(const TSharedPtr<FJsonObject>& Obj);

	/**
	 * sections 数组是否非空且不含 "all"（即调用方指定了具体子 section）。
	 */
	static bool HasSubSection(const TSharedPtr<FJsonObject>& Obj);

	/**
	 * 解析通用分页参数 offset / limit。
	 * - offset ≥ 0，不存在时取 0
	 * - limit ∈ [1, MaxLimit]，不存在时取 DefaultLimit
	 */
	static void ParseOffsetLimit(const TSharedPtr<FJsonObject>& Obj,
	                             int32& OutOffset, int32& OutLimit,
	                             int32 DefaultLimit = 100, int32 MaxLimit = 500);

	/**
	 * 计算分页切片 [OutStart, OutEnd)，可直接用于数组索引。
	 * 等价于常见的 Start = Min(Offset, Total); End = Min(Start+Limit, Total) 模板。
	 */
	static void ComputeSlice(int32 Total, int32 Offset, int32 Limit,
	                         int32& OutStart, int32& OutEnd);

	/**
	 * 统一提取 manage_* 类 Capability 的批量操作数组。
	 * 优先级：`operations` → 回退 `ops`（旧字段，过渡期兼容）→ 回退顶层 `action`+其余字段
	 * 合成单元素数组（旧单操作 manage 过渡期兼容）。
	 * Schema 只暴露 `operations`；本 helper 仅用于 Execute 内部读入，不影响对外契约。
	 */
	static TArray<TSharedPtr<FJsonValue>> ExtractOperations(const TSharedPtr<FJsonObject>& Args);
};
