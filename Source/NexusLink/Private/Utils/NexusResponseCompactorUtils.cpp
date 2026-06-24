// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusResponseCompactorUtils.h"
#include "NexusLinkSettings.h"

/** 判断值是否为可抽取的标量类型。 */
static FORCEINLINE bool IsScalarValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}
	const EJson T = Value->Type;
	return T == EJson::String || T == EJson::Number || T == EJson::Boolean || T == EJson::Null;
}

/**
 * 直接比较两个标量 FJsonValue 是否相等（不走字符串 key）。
 * 不同类型一律不等；Object/Array 一律不等（压缩不关心）。
 */
static bool AreScalarJsonValuesEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
{
	const bool bAValid = A.IsValid();
	const bool bBValid = B.IsValid();
	if (!bAValid || !bBValid)
	{
		return bAValid == bBValid;
	}
	if (A->Type != B->Type)
	{
		return false;
	}
	switch (A->Type)
	{
	case EJson::Null:    return true;
	case EJson::Boolean: return A->AsBool() == B->AsBool();
	case EJson::Number:  return A->AsNumber() == B->AsNumber();
	case EJson::String:  return A->AsString() == B->AsString();
	default:             return false;
	}
}

/** 估算"字段 + 值"在序列化 JSON 里占用的字节数（粗略，足够判断净收益）。 */
static int32 EstimateEntryBytes(const FString& Field, const TSharedPtr<FJsonValue>& Value)
{
	// "field":
	const int32 KeyCost = Field.Len() + 4;
	int32 ValCost = 0;
	if (Value.IsValid())
	{
		switch (Value->Type)
		{
		case EJson::String:
			ValCost = Value->AsString().Len() + 2;
			break;
		case EJson::Number:
			ValCost = 16;
			break;
		case EJson::Boolean:
			ValCost = 5;
			break;
		default:
			ValCost = 4;
			break;
		}
	}
	// 分隔符 + 缩进（保守估算）
	return KeyCost + ValCost + 4;
}

/** 从 Obj 中取字段；不存在返回无效 Ptr。 */
static FORCEINLINE TSharedPtr<FJsonValue> FindField(const TSharedPtr<FJsonObject>& Obj, const FString& Field)
{
	if (!Obj.IsValid())
	{
		return TSharedPtr<FJsonValue>();
	}
	return Obj->TryGetField(Field);
}

/** 进程级共享的身份字段排除集：避免 SetAutoDiscover 每次都往 per-instance TSet 里塞 13 个字符串。 */
static const TSet<FString>& GetBuiltinAutoDiscoverExclusions()
{
	static const TSet<FString> Singleton = []() {
		TSet<FString> S;
		S.Reserve(16);
		S.Add(TEXT("name"));      S.Add(TEXT("path"));    S.Add(TEXT("assetPath")); S.Add(TEXT("nodeId"));
		S.Add(TEXT("tag"));       S.Add(TEXT("message")); S.Add(TEXT("timestamp")); S.Add(TEXT("frame"));
		S.Add(TEXT("id"));        S.Add(TEXT("label"));   S.Add(TEXT("title"));     S.Add(TEXT("text"));
		S.Add(TEXT("error"));
		return S;
	}();
	return Singleton;
}

void FNexusResponseCompactorUtils::AddCandidate(const FString& FieldName)
{
	if (FieldName.IsEmpty())
	{
		return;
	}
	Candidates.AddUnique(FieldName);
}

void FNexusResponseCompactorUtils::AddForcedDefault(const FString& FieldName, const TSharedPtr<FJsonValue>& Value)
{
	if (FieldName.IsEmpty() || !Value.IsValid())
	{
		return;
	}
	ForcedDefaults.Add(FieldName, Value);
}

void FNexusResponseCompactorUtils::AddForcedDefault(const FString& FieldName, const FString& StringValue)
{
	AddForcedDefault(FieldName, MakeShared<FJsonValueString>(StringValue));
}

void FNexusResponseCompactorUtils::AddForcedDefault(const FString& FieldName, bool bBoolValue)
{
	AddForcedDefault(FieldName, MakeShared<FJsonValueBoolean>(bBoolValue));
}

void FNexusResponseCompactorUtils::SetAutoDiscover(bool bEnable, TArray<FString> AdditionalExclusions)
{
	bAutoDiscover = bEnable;
	// per-instance 仅保留调用方追加的业务特有字段；内置 13 个身份字段走进程级共享 TSet
	for (const FString& F : AdditionalExclusions)
	{
		if (!F.IsEmpty())
		{
			ExtraAutoDiscoverExclusions.Add(F);
		}
	}
}

void FNexusResponseCompactorUtils::TryCompactField(const FString& Field, TArray<TSharedPtr<FJsonValue>>& Items)
{
	// 压缩目标字段的基数在真实负载里通常 ≤ 5：用小桶线性扫替代 TMap<FString,...>，
	// 消灭 JsonValueKey 的 FString::Printf 分配与 hash 开销。
	TArray<TPair<TSharedPtr<FJsonValue>, int32>, TInlineAllocator<8>> Buckets;
	int32 HaveCount = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		if (!Item.IsValid() || Item->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> Obj = Item->AsObject();
		const TSharedPtr<FJsonValue> ItemVal = FindField(Obj, Field);
		if (!IsScalarValue(ItemVal))
		{
			continue;
		}
		++HaveCount;

		bool bMerged = false;
		for (TPair<TSharedPtr<FJsonValue>, int32>& B : Buckets)
		{
			if (AreScalarJsonValuesEqual(B.Key, ItemVal))
			{
				++B.Value;
				bMerged = true;
				break;
			}
		}
		if (!bMerged)
		{
			Buckets.Emplace(ItemVal, 1);
		}
	}

	if (HaveCount == 0)
	{
		return;
	}

	// 找主流值
	TSharedPtr<FJsonValue> TopValue;
	int32 TopCount = 0;
	for (const TPair<TSharedPtr<FJsonValue>, int32>& B : Buckets)
	{
		if (B.Value > TopCount)
		{
			TopCount = B.Value;
			TopValue = B.Key;
		}
	}

	if (TopCount < MinCount)
	{
		return;
	}
	const float Ratio = static_cast<float>(TopCount) / static_cast<float>(HaveCount);
	if (Ratio < MinMatchRatio)
	{
		return;
	}

	// 净收益评估：保守估算字段写入 defaults 的开销 + 多少条被省略
	const int32 EntryBytes = EstimateEntryBytes(Field, TopValue);
	const int32 SaveBytes = (TopCount - 1) * EntryBytes;
	if (SaveBytes < MinNetSaveBytes)
	{
		return;
	}

	// 执行抽取：所有等值条目直接按类型化比较剥离该字段（无需二次生成字符串 key）
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		if (!Item.IsValid() || Item->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> Obj = Item->AsObject();
		const TSharedPtr<FJsonValue> ItemVal = FindField(Obj, Field);
		if (ItemVal.IsValid() && AreScalarJsonValuesEqual(ItemVal, TopValue))
		{
			Obj->RemoveField(Field);
		}
	}
	Defaults->SetField(Field, TopValue);
}

void FNexusResponseCompactorUtils::CompactArray(TArray<TSharedPtr<FJsonValue>>& Items)
{
	Defaults = MakeShared<FJsonObject>();

	if (Items.Num() == 0)
	{
		return;
	}

	// 全局开关：关闭时直接跳过压缩（保留条目原状）
	if (const UNexusLinkSettings* Settings = UNexusLinkSettings::Get())
	{
		if (!Settings->bCompactResponseDefaults)
		{
			return;
		}
	}

	// 快速 bail：样本不足 MinCount 且没有任何 ForcedDefault，那么所有后续分支都会被阈值
	// 挡回，提前退出省掉一整轮 O(N×fields) 遍历。自动 Pass 下小数组场景占比极高。
	if (ForcedDefaults.Num() == 0 && Items.Num() < MinCount)
	{
		return;
	}

	// 1. 强制默认：无条件写入 defaults 并从条目里移除等值字段
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ForcedDefaults)
	{
		const FString& Field = Pair.Key;
		const TSharedPtr<FJsonValue>& DefVal = Pair.Value;
		if (!IsScalarValue(DefVal))
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			if (!Item.IsValid() || Item->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Obj = Item->AsObject();
			const TSharedPtr<FJsonValue> ItemVal = FindField(Obj, Field);
			if (ItemVal.IsValid() && AreScalarJsonValuesEqual(ItemVal, DefVal))
			{
				Obj->RemoveField(Field);
			}
		}
		Defaults->SetField(Field, DefVal);
	}

	// 数据量低于 MinCount 时不做统计抽取（仍允许强制默认）
	if (Items.Num() < MinCount)
	{
		return;
	}

	// 2. 显式候选字段统计抽取
	for (const FString& Field : Candidates)
	{
		TryCompactField(Field, Items);
	}

	// 3. 自动扫描：发现条目里所有未被处理的标量字段并尝试抽取
	if (bAutoDiscover)
	{
		const TSet<FString>& BuiltinExclusions = GetBuiltinAutoDiscoverExclusions();

		// 构建已处理字段集合，避免重复分析（Candidates / ForcedDefaults 已走过各自分支）
		TSet<FString> Handled;
		Handled.Reserve(Candidates.Num() + ForcedDefaults.Num());
		for (const FString& F : Candidates) { Handled.Add(F); }
		for (const auto& Pair : ForcedDefaults) { Handled.Add(Pair.Key); }

		// 扫描所有条目，收集尚未处理的标量字段名
		TSet<FString> Discovered;
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			if (!Item.IsValid() || Item->Type != EJson::Object)
			{
				continue;
			}
			for (const auto& KV : Item->AsObject()->Values)
			{
				const FString K(*KV.Key);
				if (Handled.Contains(K) || Discovered.Contains(K))
				{
					continue;
				}
				if (BuiltinExclusions.Contains(K) || ExtraAutoDiscoverExclusions.Contains(K))
				{
					continue;
				}
				if (IsScalarValue(KV.Value))
				{
					Discovered.Add(K);
				}
			}
		}

		for (const FString& Field : Discovered)
		{
			TryCompactField(Field, Items);
		}
	}
}

void FNexusResponseCompactorUtils::Emit(const TSharedPtr<FJsonObject>& Parent, const FString& Prefix) const
{
	if (!Parent.IsValid() || !HasDefaults())
	{
		return;
	}

	const FString DefaultsKey = Prefix.IsEmpty()
		? FString(TEXT("defaults"))
		: Prefix + TEXT("_defaults");

	Parent->SetObjectField(DefaultsKey, Defaults);
}

/** 不应被自动压缩 Pass 触发的字段名：要么语义特殊，要么会与现有协议字段冲突。 */
static bool ShouldSkipAutoCompactField(const FString& FieldName)
{
	// 协议/结构字段：MCP content 数组、已产出的 _defaults 对象等
	if (FieldName.EndsWith(TEXT("_defaults")))
	{
		return true;
	}
	// content 是 MCP 响应外层的特殊数组（type/text/image 等），不做压缩
	if (FieldName.Equals(TEXT("content")))
	{
		return true;
	}
	return false;
}

/** 递归实现：Depth 防御性限制嵌套层数，避免病态响应导致栈深。 */
static void AutoCompactRecursiveImpl(const TSharedPtr<FJsonObject>& Parent, int32 Depth)
{
	if (!Parent.IsValid() || Depth > 16)
	{
		return;
	}

	// 快照当前字段名集合，避免遍历过程中 SetObjectField 写入 _defaults 产生重入
	TArray<FString> FieldNames;
	FieldNames.Reserve(Parent->Values.Num());
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Parent->Values)
	{
		FieldNames.Add(Pair.Key);
	}

	for (const FString& Field : FieldNames)
	{
		const TSharedPtr<FJsonValue> Value = Parent->TryGetField(Field);
		if (!Value.IsValid())
		{
			continue;
		}

		if (Value->Type == EJson::Object)
		{
			AutoCompactRecursiveImpl(Value->AsObject(), Depth + 1);
			continue;
		}

		if (Value->Type != EJson::Array)
		{
			continue;
		}

		// AsArray 返回对内部 TArray 的 const 引用 —— 直接取引用用于就地修改 inner objects，
		// 不需要拷贝一份再 SetArrayField 写回（shared_ptr 的 inner FJsonObject 修改天然可见）。
		const TArray<TSharedPtr<FJsonValue>>& ItemsRef = Value->AsArray();
		const bool bIsArrayOfObjects = ItemsRef.Num() > 0 && ItemsRef[0].IsValid() && ItemsRef[0]->Type == EJson::Object;
		if (!bIsArrayOfObjects)
		{
			continue;
		}

		// 先递归进入每个子对象，处理更深层嵌套
		for (const TSharedPtr<FJsonValue>& Item : ItemsRef)
		{
			if (Item.IsValid() && Item->Type == EJson::Object)
			{
				AutoCompactRecursiveImpl(Item->AsObject(), Depth + 1);
			}
		}

		if (ShouldSkipAutoCompactField(Field))
		{
			continue;
		}

		// 工具侧已显式写入 <Field>_defaults 时跳过，避免双写
		const FString DefaultsKey = Field + TEXT("_defaults");
		if (Parent->HasField(DefaultsKey))
		{
			continue;
		}

		// CompactArray 签名要求非 const 引用（会就地移除 inner object 字段）；
		// 这里对 ItemsRef 做一次 const_cast 是安全的——Items 数组结构（元素个数与指针）
		// 不会被 CompactArray 修改，只有 inner FJsonObject 的字段会被 RemoveField 抽走。
		TArray<TSharedPtr<FJsonValue>>& Items =
			const_cast<TArray<TSharedPtr<FJsonValue>>&>(ItemsRef);

		FNexusResponseCompactorUtils Local;
		Local.SetAutoDiscover(true);
		Local.CompactArray(Items);
		Local.Emit(Parent, Field);
	}
}

void FNexusResponseCompactorUtils::AutoCompactRecursive(const TSharedPtr<FJsonObject>& Parent)
{
	if (!Parent.IsValid())
	{
		return;
	}

	// 遵循总开关：关闭时整个 Pass 直接跳过（与 CompactArray 行为一致）
	if (const UNexusLinkSettings* Settings = UNexusLinkSettings::Get())
	{
		if (!Settings->bCompactResponseDefaults)
		{
			return;
		}
	}

	AutoCompactRecursiveImpl(Parent, 0);
}

