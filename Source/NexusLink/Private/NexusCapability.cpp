// Copyright byteyang. All Rights Reserved.

#include "NexusCapability.h"
#include "NexusFeedback.h"
#include "NexusLinkSettings.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/UnrealMathUtility.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusCapability, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// InputSchema 严格校验（Run 在 required 之后、Execute 之前调用）
// ─────────────────────────────────────────────────────────────────────────────

namespace NexusCapabilitySchemaValidate
{
	static FString JoinPath(const FString& Base, const FString& Seg)
	{
		return Base.IsEmpty() ? Seg : (Base + TEXT(".") + Seg);
	}

	/** 兼容 UE5.8+ JsonObject 键类型（FSharedString）与旧版 FString：二者均支持 operator* → TCHAR*。 */
	template <typename KeyType>
	static FString KeyAsString(const KeyType& Key)
	{
		return FString(*Key);
	}

	static FString IndexPath(const FString& Base, int32 Index)
	{
		return FString::Printf(TEXT("%s[%d]"), Base.IsEmpty() ? TEXT("$") : *Base, Index);
	}

	static bool AllowsAdditionalProperties(const TSharedPtr<FJsonObject>& Schema)
	{
		// 显式 true 才放行未知键；缺省视为 false（与 FNexusSchema::Object 默认一致）
		bool bAllow = false;
		return Schema.IsValid()
			&& Schema->TryGetBoolField(TEXT("additionalProperties"), bAllow)
			&& bAllow;
	}

	static bool MatchesJsonType(const TSharedPtr<FJsonValue>& Value, const FString& TypeName, FString& OutActual)
	{
		if (!Value.IsValid() || Value->IsNull())
		{
			OutActual = TEXT("null");
			return false;
		}
		switch (Value->Type)
		{
		case EJson::Object:
			OutActual = TEXT("object");
			return TypeName.Equals(TEXT("object"), ESearchCase::IgnoreCase);
		case EJson::Array:
			OutActual = TEXT("array");
			return TypeName.Equals(TEXT("array"), ESearchCase::IgnoreCase);
		case EJson::String:
			OutActual = TEXT("string");
			return TypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase);
		case EJson::Boolean:
			OutActual = TEXT("boolean");
			return TypeName.Equals(TEXT("boolean"), ESearchCase::IgnoreCase);
		case EJson::Number:
		{
			double Num = 0.0;
			Value->TryGetNumber(Num);
			const bool bIntegral = FMath::IsNearlyEqual(Num, FMath::RoundToDouble(Num));
			OutActual = bIntegral ? TEXT("integer") : TEXT("number");
			if (TypeName.Equals(TEXT("number"), ESearchCase::IgnoreCase))
			{
				return true;
			}
			if (TypeName.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
			{
				return bIntegral;
			}
			return false;
		}
		default:
			OutActual = TEXT("unknown");
			return false;
		}
	}

	static bool ValidateValue(
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonObject>& Schema,
		const FString& Path,
		FString& OutError);

	static bool ValidateObject(
		const TSharedPtr<FJsonObject>& Obj,
		const TSharedPtr<FJsonObject>& Schema,
		const FString& Path,
		FString& OutError)
	{
		if (!Obj.IsValid() || !Schema.IsValid())
		{
			OutError = FString::Printf(TEXT("字段 '%s' 校验失败（对象或 Schema 无效）"),
				Path.IsEmpty() ? TEXT("$") : *Path);
			return false;
		}

		const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
		const bool bHasProps = Schema->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr && PropsPtr->IsValid();
		const TSharedPtr<FJsonObject> Props = bHasProps ? *PropsPtr : nullptr;
		const bool bAllowAdditional = AllowsAdditionalProperties(Schema);

		// 未知键
		for (const auto& KV : Obj->Values)
		{
			const bool bDeclared = Props.IsValid() && Props->HasField(KV.Key);
			if (!bDeclared && !bAllowAdditional)
			{
				const FString FullPath = JoinPath(Path, KeyAsString(KV.Key));
				OutError = FString::Printf(TEXT("未知参数 '%s'（additionalProperties=false）"), *FullPath);
				return false;
			}
		}

		// required（嵌套；顶层空串拒绝仍由 Run 前置逻辑负责）
		const TArray<TSharedPtr<FJsonValue>>* ReqArr = nullptr;
		if (Schema->TryGetArrayField(TEXT("required"), ReqArr) && ReqArr)
		{
			for (const TSharedPtr<FJsonValue>& ReqV : *ReqArr)
			{
				FString Field;
				if (!ReqV.IsValid() || !ReqV->TryGetString(Field) || Field.IsEmpty())
				{
					continue;
				}
				const TSharedPtr<FJsonValue> FieldVal = Obj->TryGetField(Field);
				if (!FieldVal.IsValid() || FieldVal->IsNull())
				{
					OutError = FString::Printf(TEXT("缺少必填字段 '%s'"), *JoinPath(Path, Field));
					return false;
				}
			}
		}

		// 已声明字段递归
		if (Props.IsValid())
		{
			for (const auto& PropKV : Props->Values)
			{
				const TSharedPtr<FJsonValue> FieldVal = Obj->TryGetField(PropKV.Key);
				if (!FieldVal.IsValid() || FieldVal->IsNull())
				{
					continue;
				}
				const TSharedPtr<FJsonObject>* FieldSchemaPtr = nullptr;
				if (!PropKV.Value.IsValid() || PropKV.Value->Type != EJson::Object
					|| !PropKV.Value->TryGetObject(FieldSchemaPtr) || !FieldSchemaPtr || !FieldSchemaPtr->IsValid())
				{
					continue;
				}
				if (!ValidateValue(FieldVal, *FieldSchemaPtr, JoinPath(Path, KeyAsString(PropKV.Key)), OutError))
				{
					return false;
				}
			}
		}

		return true;
	}

	static bool ValidateValue(
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonObject>& Schema,
		const FString& Path,
		FString& OutError)
	{
		if (!Schema.IsValid())
		{
			return true;
		}

		FString ExpectedType;
		if (Schema->TryGetStringField(TEXT("type"), ExpectedType) && !ExpectedType.IsEmpty())
		{
			FString ActualType;
			if (!MatchesJsonType(Value, ExpectedType, ActualType))
			{
				OutError = FString::Printf(TEXT("字段 '%s' 类型应为 %s，实际为 %s"),
					Path.IsEmpty() ? TEXT("$") : *Path, *ExpectedType, *ActualType);
				return false;
			}
		}

		// enum（通常挂在 string 上）
		const TArray<TSharedPtr<FJsonValue>>* EnumArr = nullptr;
		if (Schema->TryGetArrayField(TEXT("enum"), EnumArr) && EnumArr && EnumArr->Num() > 0)
		{
			FString StrVal;
			if (!Value.IsValid() || !Value->TryGetString(StrVal))
			{
				OutError = FString::Printf(TEXT("字段 '%s' 的 enum 约束仅支持 string"),
					Path.IsEmpty() ? TEXT("$") : *Path);
				return false;
			}
			bool bInEnum = false;
			for (const TSharedPtr<FJsonValue>& E : *EnumArr)
			{
				FString Allowed;
				if (E.IsValid() && E->TryGetString(Allowed) && Allowed == StrVal)
				{
					bInEnum = true;
					break;
				}
			}
			if (!bInEnum)
			{
				OutError = FString::Printf(TEXT("字段 '%s' 值 '%s' 不在 enum 内"),
					Path.IsEmpty() ? TEXT("$") : *Path, *StrVal);
				return false;
			}
		}

		if (Value.IsValid() && Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!Value->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
			{
				OutError = FString::Printf(TEXT("字段 '%s' 不是有效 object"),
					Path.IsEmpty() ? TEXT("$") : *Path);
				return false;
			}
			return ValidateObject(*ObjPtr, Schema, Path, OutError);
		}

		if (Value.IsValid() && Value->Type == EJson::Array)
		{
			// UE4 Dom：FJsonValue 无 TryGetArray，用 AsArray（调用前已判 Type）
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			const TSharedPtr<FJsonObject>* ItemsSchemaPtr = nullptr;
			if (Schema->TryGetObjectField(TEXT("items"), ItemsSchemaPtr) && ItemsSchemaPtr && ItemsSchemaPtr->IsValid())
			{
				for (int32 i = 0; i < Arr.Num(); ++i)
				{
					if (!ValidateValue(Arr[i], *ItemsSchemaPtr, IndexPath(Path, i), OutError))
					{
						return false;
					}
				}
			}
		}

		return true;
	}

	static bool ValidateArgs(
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Schema,
		FString& OutError)
	{
		if (!Schema.IsValid())
		{
			return true;
		}
		// 根始终按 object 校验；若 Schema 声明了非 object type 则先拦一层
		FString RootType;
		if (Schema->TryGetStringField(TEXT("type"), RootType)
			&& !RootType.IsEmpty()
			&& !RootType.Equals(TEXT("object"), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("InputSchema.type 应为 object，实际为 %s"), *RootType);
			return false;
		}
		return ValidateObject(Args, Schema, FString(), OutError);
	}
} // namespace NexusCapabilitySchemaValidate

// manage 收尾 mixin：向 Schema 注入 saveToDisk；BP/ABP/WBP 另注入 compile
namespace NexusManageFinalizeMixin
{
	static bool IsManageAssetCap(const FString& Name)
	{
		return Name.StartsWith(TEXT("manage_asset_"));
	}

	static bool SupportsCompile(const FString& Name)
	{
		return Name == TEXT("manage_asset_blueprint")
			|| Name == TEXT("manage_asset_anim_blueprint")
			|| Name == TEXT("manage_asset_user_widget");
	}

	static void InjectOptionalBool(TSharedPtr<FJsonObject>& Schema, const TCHAR* Name, const TCHAR* Desc)
	{
		if (!Schema.IsValid())
		{
			return;
		}
		TSharedPtr<FJsonObject> Props;
		const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
		if (Schema->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr && PropsPtr->IsValid())
		{
			Props = *PropsPtr;
		}
		else
		{
			Props = MakeShared<FJsonObject>();
			Schema->SetObjectField(TEXT("properties"), Props);
		}
		if (Props->HasField(Name))
		{
			return;
		}
		Props->SetObjectField(Name, FNexusSchema::Bool(Desc, true, false));
	}

	static void InjectSchema(FNexusCapabilityDefinition& Def)
	{
		if (!IsManageAssetCap(Def.Name))
		{
			return;
		}
		InjectOptionalBool(Def.InputSchema, TEXT("saveToDisk"), TEXT("成功后将包保存到磁盘"));
		if (SupportsCompile(Def.Name))
		{
			InjectOptionalBool(Def.InputSchema, TEXT("compile"), TEXT("按需编译蓝图（仅 BP/ABP/WBP）"));
		}
	}

	static bool HasSuccessfulEntry(const FCapabilityResult& Result)
	{
		for (const TSharedPtr<FJsonValue>& V : Result.Entries)
		{
			if (!V.IsValid() || V->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (Obj.IsValid() && !Obj->HasField(TEXT("error")))
			{
				return true;
			}
		}
		return false;
	}

	static void ApplyIfRequested(
		const FString& CapName,
		const TSharedPtr<FJsonObject>& Args,
		FCapabilityResult& Result)
	{
		if (!IsManageAssetCap(CapName) || !Result.FatalError.IsEmpty() || !HasSuccessfulEntry(Result))
		{
			return;
		}

		bool bCompile = false;
		bool bSaveToDisk = false;
		Args->TryGetBoolField(TEXT("compile"), bCompile);
		Args->TryGetBoolField(TEXT("saveToDisk"), bSaveToDisk);
		if (!bCompile && !bSaveToDisk)
		{
			return;
		}

		FString AssetPath;
		Args->TryGetStringField(TEXT("assetPath"), AssetPath);
		if (AssetPath.IsEmpty())
		{
			return;
		}

		if (!Result.TopFields.IsValid())
		{
			Result.TopFields = MakeShared<FJsonObject>();
		}
		FNexusAssetUtils::ApplyManageFinalize(AssetPath, bCompile, bSaveToDisk, Result.TopFields);
	}
} // namespace NexusManageFinalizeMixin

// ─────────────────────────────────────────────────────────────────────────────

const FNexusCapabilityDefinition& FNexusCapability::GetDefinition() const
{
	if (!bDefBuilt)
	{
		BuildDefinition(CachedDef);
		NexusManageFinalizeMixin::InjectSchema(CachedDef);
		// Runtime 宿主范围：自动补分类标签，BuildDefinition 无需手写（亦可手写，幂等）
		if (GetHostScope() == ENexusCapabilityHostScope::Runtime
			&& !CachedDef.HasTag(FNexusMcpTags::Runtime))
		{
			CachedDef.Tags.Add(FNexusMcpTags::Runtime);
		}
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
				// 用 TryGetField，勿直接 Values.Find(FString)：UE 5.8+ Values 键为 FSharedString
				const TSharedPtr<FJsonValue> FieldVal = Args->TryGetField(Field);
				if (!FieldVal.IsValid() || FieldVal->IsNull())
				{
					return FCapabilityResult::MakeArgInvalid(FString::Printf(
						TEXT("缺少必填字段 '%s'（Capability '%s'）"), *Field, *Def.Name));
				}
				if (FieldVal->Type == EJson::String)
				{
					FString StrVal;
					FieldVal->TryGetString(StrVal);
					if (StrVal.IsEmpty())
					{
						return FCapabilityResult::MakeArgInvalid(FString::Printf(
							TEXT("必填字段 '%s' 不能为空（Capability '%s'）"), *Field, *Def.Name));
					}
				}
			}
		}

		// 按 InputSchema 递归严格校验（未知键 / type / required / enum / items）
		FString SchemaErr;
		if (!NexusCapabilitySchemaValidate::ValidateArgs(Args, Def.InputSchema, SchemaErr))
		{
			return FCapabilityResult::MakeArgInvalid(FString::Printf(
				TEXT("%s（Capability '%s'）"), *SchemaErr, *Def.Name));
		}
	}

	const double StartTime = FPlatformTime::Seconds();
	FCapabilityResult Result = Execute(Args);
	NexusManageFinalizeMixin::ApplyIfRequested(Def.Name, Args, Result);
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
