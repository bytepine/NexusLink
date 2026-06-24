// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Result
#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Capability Execute() 返回壳统一助手。
 *
 * 消除所有 Capability 内重复的 P1 样板代码：
 *   FCapabilityResult _R;
 *   TSharedPtr<FJsonObject> _Top = MakeShared<FJsonObject>();
 *   [&](...) { ... }(_R.Entries, _Top, _R.FatalError);
 *   if (_Top->Values.Num() > 0) { _R.TopFields = _Top; }
 *   return _R;
 *
 * 以及 P5 单行 entry 错误助手：
 *   OutEntry->SetStringField(TEXT("error"), ...);
 *   OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
 */
class NEXUSLINK_API FNexusCapabilityResultBuilder final
{
public:
	FNexusCapabilityResultBuilder() = delete;

	/**
	 * 执行 Capability 业务 lambda，自动归并 TopFields、FatalError，返回完整结果。
	 * 模板化以兼容泛型 lambda（auto& 参数）——UE5.4+ 严格模式下 TFunction 不接受泛型 lambda 直接转换。
	 */
	template<typename TFn>
	static FCapabilityResult Build(TFn Fn)
	{
		FCapabilityResult R;
		TSharedPtr<FJsonObject> Top = MakeShared<FJsonObject>();
		Fn(R.Entries, Top, R.FatalError);
		if (Top->Values.Num() > 0)
			R.TopFields = Top;
		return R;
	}

	/**
	 * 向 OutEntries 追加一个只含 error 字段的 entry（P5 单行助手）。
	 * 等价于：OutEntry->SetStringField("error", Msg); OutEntries.Add(...)。
	 */
	static void AddEntryError(TArray<TSharedPtr<FJsonValue>>& OutEntries, const FString& Msg);

	/** 向 OutEntries 追加一个 entry（确保 Entry 有效才追加）。 */
	static void AddEntry(TArray<TSharedPtr<FJsonValue>>& OutEntries,
	                     const TSharedPtr<FJsonObject>& Entry);
};
