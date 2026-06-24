// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "NexusMcpTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

/**
 * FCapabilityResult → FNexusMcpToolResult 适配工具。
 * 供 call_capability（单条模式）和 MultiTool 模式的 Dispatcher 共用。
 */
namespace NexusCapResultAdapter
{
	/**
	 * 将 Capability 执行结果转换为 MCP Tool 结果。
	 * 聚合逻辑：
	 *   - FatalError 非空 → bIsError=true, ErrorText=FatalError
	 *   - 正常 → TopFields 作为 StructuredContent，Entries 写入 results[]
	 *
	 * @param CapResult     Capability 的 Run() 返回值
	 * @param CapName       Capability 名称（用于错误信息）
	 */
	inline FNexusMcpToolResult Convert(const FCapabilityResult& CapResult, const FString& CapName)
	{
		FNexusMcpToolResult Result;

		if (!CapResult.FatalError.IsEmpty())
		{
			Result.bIsError  = true;
			Result.ErrorText = CapResult.FatalError;
			return Result;
		}

		TSharedPtr<FJsonObject> Top = CapResult.TopFields.IsValid()
			? CapResult.TopFields
			: MakeShared<FJsonObject>();
		if (CapResult.Entries.Num() > 0)
		{
			Top->SetArrayField(TEXT("results"), CapResult.Entries);
		}

		Result.StructuredContent = Top;

		// 序列化为 OutputText（与 call_capability 行为一致）
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result.OutputText);
		FJsonSerializer::Serialize(Top.ToSharedRef(), W);

		return Result;
	}
}
