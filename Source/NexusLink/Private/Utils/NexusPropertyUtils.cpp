// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPropertyUtils.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/UnrealType.h"

// ====================================================================
// 路径段解析 —— 支持 "Items[0]"、"Map[\"key\"]"、"Matrix[0][1]" 等容器访问语法
// 语法规则：
//   - 按 '.' 切分段（见调用点），本函数只处理单段内的 `[ ... ]` 下标
//   - 方括号内若以 "..." 或 '...' 包裹，视为字符串键（剥去引号）
//   - 否则原样保留，交由 ApplyAccessors 按容器类型判定（数字 → Array index / Map int key，其余 → Map 字符串键）
//   - 多对方括号串联（`A[0][1]`）按出现顺序逐级应用
// ====================================================================
static bool ParseSegment(const FString& Seg, FString& OutName, TArray<FString>& OutAcc, FString& OutError)
{
	OutName.Reset();
	OutAcc.Reset();

	int32 BracketOpen = INDEX_NONE;
	if (!Seg.FindChar(TEXT('['), BracketOpen))
	{
		OutName = Seg;
		return true;
	}
	OutName = Seg.Left(BracketOpen);
	if (OutName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Path segment missing property name: '%s'"), *Seg);
		return false;
	}

	int32 Cursor = BracketOpen;
	while (Cursor < Seg.Len())
	{
		if (Seg[Cursor] != TEXT('['))
		{
			OutError = FString::Printf(TEXT("Unexpected character '%c' after ']' in segment: '%s'"), Seg[Cursor], *Seg);
			return false;
		}
		const int32 CloseIdx = Seg.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor + 1);
		if (CloseIdx == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Unclosed '[' in path segment: '%s'"), *Seg);
			return false;
		}
		FString Inner = Seg.Mid(Cursor + 1, CloseIdx - Cursor - 1);
		// 双引号/单引号包裹的字符串键：剥壳
		if (Inner.Len() >= 2)
		{
			const TCHAR First = Inner[0];
			const TCHAR Last = Inner[Inner.Len() - 1];
			if ((First == TEXT('"') && Last == TEXT('"')) || (First == TEXT('\'') && Last == TEXT('\'')))
			{
				Inner = Inner.Mid(1, Inner.Len() - 2);
			}
		}
		OutAcc.Add(Inner);
		Cursor = CloseIdx + 1;
	}
	return true;
}

/** 将 Map 元素的 Key 序列化为字符串用于匹配（剥去 ExportText 对 FString/FName 加的包装引号）。 */
static FString ExportMapKeyAsString(const FProperty* KeyProp, const void* KeyData)
{
	FString KeyStr;
	FNexusPropertyUtils::ExportText(KeyProp, KeyStr, KeyData);
	if (KeyStr.Len() >= 2 && KeyStr.StartsWith(TEXT("\"")) && KeyStr.EndsWith(TEXT("\"")))
	{
		KeyStr = KeyStr.Mid(1, KeyStr.Len() - 2);
	}
	return KeyStr;
}

/** 按 [idx] / [key] 序列推进 (Prop, ValuePtr) 到元素指针：
 *   - FArrayProperty  → 数字下标 → Inner 元素
 *   - FMapProperty    → 与 KeyProp ExportText 后的字符串匹配 → ValueProp 元素
 *   - FSetProperty    → 按元素 ExportText 匹配 → Inner 元素
 *   其他类型属性不可下标。每一步结束后 (Prop, ValuePtr) 表示该元素自身。 */
static bool ApplyAccessors(FProperty*& InOutProp, void*& InOutPtr, const TArray<FString>& Accessors, FString& OutError)
{
	for (const FString& Acc : Accessors)
	{
		if (!InOutProp) { OutError = TEXT("Null property while applying accessors"); return false; }

		if (FArrayProperty* Arr = CastField<FArrayProperty>(InOutProp))
		{
			if (!Acc.IsNumeric())
			{
				OutError = FString::Printf(TEXT("Array '%s' expects numeric index, got: [%s]"), *Arr->GetName(), *Acc);
				return false;
			}
			const int32 Index = FCString::Atoi(*Acc);
			FScriptArrayHelper Helper(Arr, InOutPtr);
			if (!Helper.IsValidIndex(Index))
			{
				OutError = FString::Printf(TEXT("Array '%s' index [%d] out of range (size=%d)"), *Arr->GetName(), Index, Helper.Num());
				return false;
			}
			InOutProp = Arr->Inner;
			InOutPtr  = Helper.GetRawPtr(Index);
			continue;
		}

		if (FMapProperty* Map = CastField<FMapProperty>(InOutProp))
		{
			FScriptMapHelper Helper(Map, InOutPtr);
			int32 FoundPairIdx = INDEX_NONE;
			for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
			{
				if (!Helper.IsValidIndex(i)) continue;
				const FString KeyStr = ExportMapKeyAsString(Map->KeyProp, Helper.GetKeyPtr(i));
				if (KeyStr == Acc)
				{
					FoundPairIdx = i;
					break;
				}
			}
			if (FoundPairIdx == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Map '%s' key not found: [%s]"), *Map->GetName(), *Acc);
				return false;
			}
			InOutProp = Map->ValueProp;
			InOutPtr  = Helper.GetValuePtr(FoundPairIdx);
			continue;
		}

		if (FSetProperty* Set = CastField<FSetProperty>(InOutProp))
		{
			FScriptSetHelper Helper(Set, InOutPtr);
			int32 FoundIdx = INDEX_NONE;
			for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
			{
				if (!Helper.IsValidIndex(i)) continue;
				FString ElemStr;
				FNexusPropertyUtils::ExportText(Set->ElementProp, ElemStr, Helper.GetElementPtr(i));
				if (ElemStr.Len() >= 2 && ElemStr.StartsWith(TEXT("\"")) && ElemStr.EndsWith(TEXT("\"")))
				{
					ElemStr = ElemStr.Mid(1, ElemStr.Len() - 2);
				}
				if (ElemStr == Acc)
				{
					FoundIdx = i;
					break;
				}
			}
			if (FoundIdx == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Set '%s' element not found: [%s]"), *Set->GetName(), *Acc);
				return false;
			}
			InOutProp = Set->ElementProp;
			InOutPtr  = Helper.GetElementPtr(FoundIdx);
			continue;
		}

		OutError = FString::Printf(TEXT("Property '%s' is not indexable by [%s]"), *InOutProp->GetName(), *Acc);
		return false;
	}
	return true;
}

// --- 结构体内路径解析（前向声明供 ResolvePropertyRead 调用） ---

static bool ResolveStructRead(
	UScriptStruct* Struct,
	const void* StructPtr,
	const FString& PropName,
	const TArray<FString>& Remaining,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString SegName;
	TArray<FString> Accessors;
	if (!ParseSegment(PropName, SegName, Accessors, OutError)) return false;

	FProperty* Prop = Struct->FindPropertyByName(*SegName);
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on struct %s"), *SegName, *Struct->GetName());
		return false;
	}

	void* ValuePtr = const_cast<void*>(Prop->ContainerPtrToValuePtr<void>(StructPtr));
	if (!ApplyAccessors(Prop, ValuePtr, Accessors, OutError)) return false;

	if (Remaining.Num() > 0)
	{
		FStructProperty* SubStruct = CastField<FStructProperty>(Prop);
		if (SubStruct)
		{
			return ResolveStructRead(SubStruct->Struct, ValuePtr, Remaining[0],
				TArray<FString>(Remaining.GetData() + 1, Remaining.Num() - 1), OutResult, OutError);
		}
		FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
		if (ObjProp)
		{
			UObject* SubObj = ObjProp->GetObjectPropertyValue(ValuePtr);
			if (!SubObj)
			{
				OutError = FString::Printf(TEXT("Property '%s' has null object reference"), *PropName);
				return false;
			}
			return FNexusPropertyUtils::ResolvePropertyRead(SubObj, Remaining[0],
				TArray<FString>(Remaining.GetData() + 1, Remaining.Num() - 1), OutResult, OutError);
		}
		OutError = FString::Printf(TEXT("Property '%s' is neither struct nor object reference; cannot descend"), *PropName);
		return false;
	}

	// 结构体 → 输出 value + 列出子属性
	FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (StructProp)
	{
		OutResult->SetStringField(TEXT("name"), PropName);
		OutResult->SetStringField(TEXT("type"), StructProp->Struct->GetName());

		// 自动导出结构体的序列化值（FVector/FRotator 等常见结构直接可读）
		FString ValueStr;
		FNexusPropertyUtils::ExportText(Prop, ValueStr, ValuePtr);
		if (!ValueStr.IsEmpty()) { OutResult->SetStringField(TEXT("value"), ValueStr); }

		TArray<TSharedPtr<FJsonValue>> Children;
		for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
		{
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), It->GetName());
			C->SetStringField(TEXT("type"), It->GetCPPType());
			Children.Add(MakeShared<FJsonValueObject>(C));
		}
		OutResult->SetArrayField(TEXT("children"), Children);
		return true;
	}

	// 叶子
	FString ValueStr;
	FNexusPropertyUtils::ExportText(Prop, ValueStr, ValuePtr);
	OutResult->SetStringField(TEXT("name"), PropName);
	OutResult->SetStringField(TEXT("type"), Prop->GetCPPType());
	if (!ValueStr.IsEmpty()) { OutResult->SetStringField(TEXT("value"), ValueStr); }
	OutResult->SetBoolField(TEXT("isLeaf"), true);
	return true;
}

// --- UFunction() 语义 ---
// 支持 propertyPath 尾段写成 "Foo()"：调用 BlueprintPure / Const 且无入参的 UFUNCTION，
// 返回值作为叶子输出。写（ResolvePropertyWrite）侧不支持（函数无意义"写入"）。
static bool InvokeZeroArgUFunction(
	UObject* Obj,
	const FString& DisplayName,
	const FString& FnName,
	const TArray<FString>& Remaining,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	UFunction* Fn = Obj->FindFunction(*FnName);
	if (!Fn)
	{
		OutError = FString::Printf(TEXT("UFUNCTION '%s' not found on %s"), *FnName, *Obj->GetClass()->GetName());
		return false;
	}

	// 安全闸：仅放行 Pure / Const 函数，避免在 get_property 里产生副作用
	const bool bSafe = Fn->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const);
	if (!bSafe)
	{
		OutError = FString::Printf(
			TEXT("UFUNCTION '%s' on %s is neither BlueprintPure nor Const; refusing to invoke from get_property"),
			*FnName, *Obj->GetClass()->GetName());
		return false;
	}

	int32 NumInputParms = 0;
	FProperty* ReturnProp = nullptr;
	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm)) { ReturnProp = *It; continue; }
		++NumInputParms;
	}
	if (NumInputParms > 0)
	{
		OutError = FString::Printf(
			TEXT("UFUNCTION '%s' has %d input param(s); only zero-argument calls are supported"),
			*FnName, NumInputParms);
		return false;
	}
	if (Remaining.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("UFUNCTION '%s' result does not support nested path access"), *FnName);
		return false;
	}

	uint8* Buffer = static_cast<uint8*>(FMemory_Alloca(Fn->ParmsSize));
	FMemory::Memzero(Buffer, Fn->ParmsSize);
	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Buffer);
	}

	Obj->ProcessEvent(Fn, Buffer);

	OutResult->SetStringField(TEXT("name"), DisplayName);
	OutResult->SetBoolField(TEXT("isLeaf"), true);
	OutResult->SetBoolField(TEXT("isFunction"), true);
	if (ReturnProp)
	{
		FString ValueStr;
		FNexusPropertyUtils::ExportText(ReturnProp, ValueStr, ReturnProp->ContainerPtrToValuePtr<void>(Buffer));
		OutResult->SetStringField(TEXT("type"), ReturnProp->GetCPPType());
		if (!ValueStr.IsEmpty()) { OutResult->SetStringField(TEXT("value"), ValueStr); }
	}
	else
	{
		OutResult->SetStringField(TEXT("type"), TEXT("void"));
	}

	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Buffer);
	}
	return true;
}

// --- 公共 API：读取 ---

bool FNexusPropertyUtils::ResolvePropertyRead(
	UObject* Obj,
	const FString& PropName,
	const TArray<FString>& Remaining,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!Obj)
	{
		OutError = TEXT("Object is null");
		return false;
	}

	// UFunction 调用语法：propertyPath 段以 "()" 结尾
	if (PropName.EndsWith(TEXT("()")))
	{
		const FString FnName = PropName.LeftChop(2);
		return InvokeZeroArgUFunction(Obj, PropName, FnName, Remaining, OutResult, OutError);
	}

	FString SegName;
	TArray<FString> Accessors;
	if (!ParseSegment(PropName, SegName, Accessors, OutError)) return false;

	FProperty* Prop = Obj->GetClass()->FindPropertyByName(*SegName);
	if (!Prop)
	{
		// 区分三种情况：是 UFUNCTION / 是 private 无反射字段 / 真的不存在
		if (UFunction* Fn = Obj->FindFunction(*SegName))
		{
			const bool bSafe = Fn->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const);
			OutError = FString::Printf(
				TEXT("'%s' is a UFUNCTION on %s; append '()' to invoke it%s"),
				*SegName, *Obj->GetClass()->GetName(),
				bSafe ? TEXT("") : TEXT(" (note: function is not BlueprintPure/Const so get_property will still refuse)"));
		}
		else
		{
			OutError = FString::Printf(
				TEXT("Property '%s' not found on %s (not a UPROPERTY; private C++ fields without UPROPERTY are not reflected)"),
				*SegName, *Obj->GetClass()->GetName());
		}
		return false;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
	if (!ApplyAccessors(Prop, ValuePtr, Accessors, OutError)) return false;

	if (Remaining.Num() > 0)
	{
		// 结构体
		FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (StructProp)
		{
			return ResolveStructRead(StructProp->Struct, ValuePtr, Remaining[0],
				TArray<FString>(Remaining.GetData() + 1, Remaining.Num() - 1), OutResult, OutError);
		}

		// 对象引用
		FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
		if (ObjProp)
		{
			UObject* SubObj = ObjProp->GetObjectPropertyValue(ValuePtr);
			if (!SubObj)
			{
				OutError = FString::Printf(TEXT("Property '%s' has null object reference"), *PropName);
				return false;
			}
			return FNexusPropertyUtils::ResolvePropertyRead(SubObj, Remaining[0],
				TArray<FString>(Remaining.GetData() + 1, Remaining.Num() - 1), OutResult, OutError);
		}

		OutError = FString::Printf(TEXT("Property '%s' does not support nested access"), *PropName);
		return false;
	}

	// 结构体 → 输出 value + 列出子属性
	FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (StructProp)
	{
		OutResult->SetStringField(TEXT("name"), PropName);
		OutResult->SetStringField(TEXT("type"), StructProp->Struct->GetName());

		FString ValueStr;
		FNexusPropertyUtils::ExportText(Prop, ValueStr, ValuePtr);
		if (!ValueStr.IsEmpty()) { OutResult->SetStringField(TEXT("value"), ValueStr); }

		TArray<TSharedPtr<FJsonValue>> Children;
		for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
		{
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), It->GetName());
			C->SetStringField(TEXT("type"), It->GetCPPType());
			Children.Add(MakeShared<FJsonValueObject>(C));
		}
		OutResult->SetArrayField(TEXT("children"), Children);
		return true;
	}

	// 叶子
	FString ValueStr;
	FNexusPropertyUtils::ExportText(Prop, ValueStr, ValuePtr);
	OutResult->SetStringField(TEXT("name"), PropName);
	OutResult->SetStringField(TEXT("type"), Prop->GetCPPType());
	if (!ValueStr.IsEmpty()) { OutResult->SetStringField(TEXT("value"), ValueStr); }
	OutResult->SetBoolField(TEXT("isLeaf"), true);
	return true;
}

// --- 结构体内写入路径解析 ---

static bool ResolveStructWrite(
	UScriptStruct* Struct,
	void* StructPtr,
	const TArray<FString>& PathSegments,
	int32 SegmentIdx,
	FProperty*& OutProp,
	void*& OutValuePtr,
	FString& OutError)
{
	FString SegName;
	TArray<FString> Accessors;
	if (!ParseSegment(PathSegments[SegmentIdx], SegName, Accessors, OutError)) return false;

	FProperty* P = Struct->FindPropertyByName(*SegName);
	if (!P)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on struct %s"), *SegName, *Struct->GetName());
		return false;
	}
	void* VP = P->ContainerPtrToValuePtr<void>(StructPtr);
	if (!ApplyAccessors(P, VP, Accessors, OutError)) return false;

	if (SegmentIdx + 1 >= PathSegments.Num())
	{
		OutProp = P;
		OutValuePtr = VP;
		return true;
	}

	if (FStructProperty* SubStruct = CastField<FStructProperty>(P))
	{
		return ResolveStructWrite(SubStruct->Struct, VP, PathSegments, SegmentIdx + 1, OutProp, OutValuePtr, OutError);
	}

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(P))
	{
		UObject* SubObj = ObjProp->GetObjectPropertyValue(VP);
		if (!SubObj)
		{
			OutError = FString::Printf(TEXT("Property '%s' has null object reference"), *PathSegments[SegmentIdx]);
			return false;
		}
		return FNexusPropertyUtils::ResolvePropertyWrite(SubObj, PathSegments, SegmentIdx + 1, OutProp, OutValuePtr, OutError);
	}

	OutError = FString::Printf(TEXT("Property '%s' is neither struct nor object reference; cannot descend"), *PathSegments[SegmentIdx]);
	return false;
}

// --- 公共 API：写入 ---

bool FNexusPropertyUtils::ResolvePropertyWrite(
	UObject* Obj,
	const TArray<FString>& PathSegments,
	int32 SegmentIdx,
	FProperty*& OutProp,
	void*& OutValuePtr,
	FString& OutError)
{
	if (!Obj || SegmentIdx >= PathSegments.Num())
	{
		OutError = TEXT("Invalid path resolution state");
		return false;
	}

	FString SegName;
	TArray<FString> Accessors;
	if (!ParseSegment(PathSegments[SegmentIdx], SegName, Accessors, OutError)) return false;

	FProperty* P = Obj->GetClass()->FindPropertyByName(*SegName);
	if (!P)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *SegName, *Obj->GetClass()->GetName());
		return false;
	}
	void* VP = P->ContainerPtrToValuePtr<void>(Obj);
	if (!ApplyAccessors(P, VP, Accessors, OutError)) return false;

	if (SegmentIdx + 1 >= PathSegments.Num())
	{
		OutProp = P;
		OutValuePtr = VP;
		return true;
	}

	// 对象引用
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(P))
	{
		UObject* SubObj = ObjProp->GetObjectPropertyValue(VP);
		if (!SubObj)
		{
			OutError = FString::Printf(TEXT("Property '%s' has null object reference"), *PathSegments[SegmentIdx]);
			return false;
		}
		return FNexusPropertyUtils::ResolvePropertyWrite(SubObj, PathSegments, SegmentIdx + 1, OutProp, OutValuePtr, OutError);
	}

	// 结构体
	if (FStructProperty* StructProp = CastField<FStructProperty>(P))
	{
		return ResolveStructWrite(StructProp->Struct, VP, PathSegments, SegmentIdx + 1, OutProp, OutValuePtr, OutError);
	}

	OutError = FString::Printf(TEXT("Property '%s' does not support nested access"), *PathSegments[SegmentIdx]);
	return false;
}

// --- 列出可编辑属性 ---

void FNexusPropertyUtils::CollectEditableProperties(UObject* Obj, TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	if (!Obj) return;
	for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), It->GetName());
		O->SetStringField(TEXT("type"), It->GetCPPType());
		OutArray.Add(MakeShared<FJsonValueObject>(O));
	}
}

// --- JSON 序列化 ---

FString FNexusPropertyUtils::SerializeJson(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

bool FNexusPropertyUtils::WritePropertyAndEcho(
	UObject*               Target,
	const TArray<FString>& PathSegs,
	int32                  StartIdx,
	const FString&         NewValue,
	FString&               OutOldValue,
	FString&               OutActualValue,
	FString&               OutError)
{
	if (!Target)
	{
		OutError = TEXT("WritePropertyAndEcho: Target is null");
		return false;
	}

	FProperty* Prop = nullptr;
	void* Ptr = nullptr;
	if (!ResolvePropertyWrite(Target, PathSegs, StartIdx, Prop, Ptr, OutError))
		return false;

	// 写入前导出旧值
	ExportText(Prop, OutOldValue, Ptr);
	// 写入新值
	ImportText(Prop, NewValue, Ptr);
	// 写入后再次导出（反映实际生效值）
	ExportText(Prop, OutActualValue, Ptr);
	return true;
}
