// Copyright byteyang. All Rights Reserved.

#include "NexusCapability.h"
#include "NexusFeedback.h"
#include "NexusLinkSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusCapability, Log, All);

// ─────────────────────────────────────────────────────────────────────────────

const FNexusCapabilityDefinition& FNexusCapability::GetDefinition() const
{
	if (!bDefBuilt)
	{
		BuildDefinition(CachedDef);
		bDefBuilt = true;
	}
	return CachedDef;
}

FCapabilityResult FNexusCapability::Run(const TSharedPtr<FJsonObject>& Arguments) const
{
	// Args 兜底：防止子类 Execute 收到空指针
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid()
		? Arguments
		: MakeShared<FJsonObject>();

	// required 字段校验（从缓存 Definition 的 InputSchema 读取）
	// 字符串类型额外拒绝空串，避免 HasField=true 却 "" 漏到 Execute 被记成 call_fatal
	const FNexusCapabilityDefinition& Def = GetDefinition();
	if (Def.InputSchema.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ReqArr = nullptr;
		if (Def.InputSchema->TryGetArrayField(TEXT("required"), ReqArr) && ReqArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ReqArr)
			{
				FString Field;
				if (!V.IsValid() || !V->TryGetString(Field) || Field.IsEmpty())
				{
					continue;
				}
				const TSharedPtr<FJsonValue>* FieldVal = Args->Values.Find(Field);
				if (!FieldVal || !FieldVal->IsValid() || (*FieldVal)->IsNull())
				{
					return FCapabilityResult::MakeArgInvalid(FString::Printf(
						TEXT("缺少必填字段 '%s'（Capability '%s'）"), *Field, *Def.Name));
				}
				if ((*FieldVal)->Type == EJson::String)
				{
					FString StrVal;
					(*FieldVal)->TryGetString(StrVal);
					if (StrVal.IsEmpty())
					{
						return FCapabilityResult::MakeArgInvalid(FString::Printf(
							TEXT("必填字段 '%s' 不能为空（Capability '%s'）"), *Field, *Def.Name));
					}
				}
			}
		}
	}

	const double StartTime = FPlatformTime::Seconds();
	FCapabilityResult Result = Execute(Args);
	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	UE_LOG(LogNexusCapability, Verbose,
		TEXT("[cap] %s  entries=%d  error=%s  %.1fms"),
		*Def.Name,
		Result.Entries.Num(),
		Result.FatalError.IsEmpty() ? TEXT("none") : *Result.FatalError,
		ElapsedMs);

	// 慢调用自动埋点
	if (Result.FatalError.IsEmpty() && !Result.bIsArgInvalid)
	{
		const UNexusLinkSettings* S = UNexusLinkSettings::Get();
		if (S && S->bEnableFeedback && S->SlowCallThresholdMs > 0
			&& static_cast<int32>(ElapsedMs) > S->SlowCallThresholdMs)
		{
		FNexusFeedback::FFields F;
		// MultiTool 模式下直接调 cap，MCP tool 名即 cap 名；SearchMode 下经 call_capability 中转
		F.Tool       = (UNexusLinkSettings::Get()->ToolsListMode == ENexusToolsListMode::MultiTool)
			? Def.Name
			: TEXT("call_capability");
		F.Capability = Def.Name;
			F.Note       = FString::Printf(TEXT("%.0fms > threshold %dms"), ElapsedMs, S->SlowCallThresholdMs);
			FNexusFeedback::RecordAuto(TEXT("slow_call"), F);
		}
	}

	return Result;
}

// ── 子类样板 helper ──────────────────────────────────────────────────────────

bool FNexusCapability::RequireString(const TSharedPtr<FJsonObject>& Args,
                                     const TCHAR* Key,
                                     FString& OutValue,
                                     TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                     const TMap<FString, FString>& Locator)
{
	if (Args.IsValid() && Args->TryGetStringField(Key, OutValue) && !OutValue.IsEmpty())
	{
		return true;
	}
	EmitError(OutEntries, Locator,
		FString::Printf(TEXT("缺少或为空必填字段 '%s'"), Key));
	return false;
}

void FNexusCapability::EmitEntry(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                 const TMap<FString, FString>& Locator,
                                 const TSharedPtr<FJsonObject>& Detail)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	for (const auto& KV : Locator)
	{
		Entry->SetStringField(KV.Key, KV.Value);
	}
	if (Detail.IsValid())
	{
		for (const auto& KV : Detail->Values)
		{
			Entry->SetField(KV.Key, KV.Value);
		}
	}
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}

void FNexusCapability::EmitError(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                 const TMap<FString, FString>& Locator,
                                 const FString& Error)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	for (const auto& KV : Locator)
	{
		Entry->SetStringField(KV.Key, KV.Value);
	}
	Entry->SetStringField(TEXT("error"), Error);
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}
