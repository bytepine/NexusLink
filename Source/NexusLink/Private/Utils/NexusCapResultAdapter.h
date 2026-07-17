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
	/** 将 /Game/Foo/Bar.Bar 规范为包路径 /Game/Foo/Bar，便于与入参比较。 */
	inline FString NormalizeAssetPathKey(FString S)
	{
		S.TrimStartAndEndInline();
		// 软引用偶发引号
		S.RemoveFromStart(TEXT("'"));
		S.RemoveFromEnd(TEXT("'"));
		int32 DotIdx = INDEX_NONE;
		if (S.FindLastChar(TEXT('.'), DotIdx) && DotIdx > 0)
		{
			const FString After = S.Mid(DotIdx + 1);
			const FString Before = S.Left(DotIdx);
			int32 SlashIdx = INDEX_NONE;
			if (Before.FindLastChar(TEXT('/'), SlashIdx) && SlashIdx >= 0)
			{
				if (Before.Mid(SlashIdx + 1).Equals(After, ESearchCase::IgnoreCase))
				{
					return Before;
				}
			}
		}
		return S;
	}

	inline bool AssetPathsEquivalent(const FString& A, const FString& B)
	{
		if (A.IsEmpty() || B.IsEmpty())
		{
			return false;
		}
		if (A.Equals(B, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return NormalizeAssetPathKey(A).Equals(NormalizeAssetPathKey(B), ESearchCase::IgnoreCase);
	}

	/**
	 * 将 Capability Entries/TopFields 组装为 StructuredContent。
	 *   - Entries.Num()==1 且为 object → 字段提升到顶层（去掉 results 信封）
	 *   - Entries.Num()>1 → 写入 results[]
	 *   - 单条非 object → 回退 results[]
	 */
	inline TSharedPtr<FJsonObject> AssembleStructuredContent(const FCapabilityResult& CapResult)
	{
		TSharedPtr<FJsonObject> Top = CapResult.TopFields.IsValid()
			? CapResult.TopFields
			: MakeShared<FJsonObject>();

		if (CapResult.Entries.Num() == 1)
		{
			const TSharedPtr<FJsonValue>& Only = CapResult.Entries[0];
			if (Only.IsValid() && Only->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> EntryObj = Only->AsObject();
				if (EntryObj.IsValid())
				{
					for (const auto& Pair : EntryObj->Values)
					{
						Top->SetField(Pair.Key, Pair.Value);
					}
					return Top;
				}
			}
			Top->SetArrayField(TEXT("results"), CapResult.Entries);
		}
		else if (CapResult.Entries.Num() > 1)
		{
			Top->SetArrayField(TEXT("results"), CapResult.Entries);
		}

		return Top;
	}

	/**
	 * 省略与入参相同的身份回显：get_/manage_ 且 Args 含单个 assetPath 时，
	 * 若响应 path（或遗留 assetPath）与入参等价则删除（调用方已知）。
	 * create_/search_/save_ 等不剥离。
	 */
	inline void StripRedundantPathEcho(
		const TSharedPtr<FJsonObject>& Content,
		const TSharedPtr<FJsonObject>& Args,
		const FString& CapName)
	{
		if (!Content.IsValid() || !Args.IsValid() || CapName.IsEmpty())
		{
			return;
		}
		if (!CapName.StartsWith(TEXT("get_")) && !CapName.StartsWith(TEXT("manage_")))
		{
			return;
		}

		FString Requested;
		if (!Args->TryGetStringField(TEXT("assetPath"), Requested) || Requested.IsEmpty())
		{
			return;
		}

		auto StripObj = [&Requested](const TSharedPtr<FJsonObject>& Obj)
		{
			if (!Obj.IsValid())
			{
				return;
			}
			FString Echoed;
			if (Obj->TryGetStringField(TEXT("path"), Echoed)
				|| Obj->TryGetStringField(TEXT("assetPath"), Echoed))
			{
				if (AssetPathsEquivalent(Echoed, Requested))
				{
					Obj->RemoveField(TEXT("path"));
					Obj->RemoveField(TEXT("assetPath"));
				}
			}
		};

		StripObj(Content);

		const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
		if (Content->TryGetArrayField(TEXT("results"), Results) && Results)
		{
			for (const TSharedPtr<FJsonValue>& V : *Results)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (V.IsValid() && V->TryGetObject(Obj) && Obj)
				{
					StripObj(*Obj);
				}
			}
		}
	}

	/**
	 * 将 Capability 执行结果转换为 MCP Tool 结果。
	 * 聚合逻辑：
	 *   - FatalError 非空 → bIsError=true, ErrorText=FatalError
	 *   - 正常 → AssembleStructuredContent（单条提升 / 多条 results[]）
	 *   - 若传入 Args：对 get_/manage_ 省略与入参等价的 path 回显
	 */
	inline FNexusMcpToolResult Convert(
		const FCapabilityResult& CapResult,
		const FString& CapName,
		const TSharedPtr<FJsonObject>& Args = nullptr)
	{
		FNexusMcpToolResult Result;

		if (!CapResult.FatalError.IsEmpty())
		{
			Result.bIsError  = true;
			Result.ErrorText = CapResult.FatalError;
			return Result;
		}

		TSharedPtr<FJsonObject> Top = AssembleStructuredContent(CapResult);
		StripRedundantPathEcho(Top, Args, CapName);
		Result.StructuredContent = Top;

		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result.OutputText);
		FJsonSerializer::Serialize(Top.ToSharedRef(), W);

		return Result;
	}
}
