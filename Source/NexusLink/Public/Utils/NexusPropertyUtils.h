// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Reflection
#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

/**
 * UObject 属性路径解析公共工具。
 * 供 Blueprint / Widget / DataAsset 等工具共用。
 */
class NEXUSLINK_API FNexusPropertyUtils
{
public:
	/**
	 * 从 UObject 上按路径段（A.B.C）递归解析属性，用于读取。
	 * - 叶子属性：填充 name/type/value/isLeaf=true
	 * - 结构体属性：填充 name/type/children[]/isLeaf=false
	 */
	static bool ResolvePropertyRead(
		UObject* Obj,
		const FString& PropName,
		const TArray<FString>& Remaining,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError);

	/** 从 UObject 上按路径段列表（全路径）递归定位属性指针，用于写入。 */
	static bool ResolvePropertyWrite(
		UObject* Obj,
		const TArray<FString>& PathSegments,
		int32 SegmentIdx,
		FProperty*& OutProp,
		void*& OutValuePtr,
		FString& OutError);

	/** 列出 UObject 上所有可编辑属性的名称和类型（单层，不递归）。 */
	static void CollectEditableProperties(
		UObject* Obj,
		TArray<TSharedPtr<FJsonValue>>& OutArray);

	/** 将 FJsonObject 序列化为格式化 JSON 字符串。 */
	static FString SerializeJson(const TSharedPtr<FJsonObject>& Obj);

	// ── 跨版本属性序列化兼容 ──
	static void ExportText(const FProperty* Prop, FString& OutValue, const void* Data)
	{
#if NX_UE_HAS_EXPORT_TEXT_DIRECT
		Prop->ExportText_Direct(OutValue, Data, nullptr, nullptr, PPF_None);
#elif NX_UE_HAS_EXPORT_TEXT_ITEM_DIR
		Prop->ExportTextItem_Direct(OutValue, Data, nullptr, nullptr, PPF_None);
#else
		Prop->ExportTextItem(OutValue, Data, nullptr, nullptr, PPF_None);
#endif
	}

	static void ImportText(const FProperty* Prop, const FString& Value, void* Data)
	{
#if NX_UE_HAS_IMPORT_TEXT_DIRECT
		Prop->ImportText_Direct(*Value, Data, nullptr, PPF_None);
#else
		Prop->ImportText(*Value, Data, PPF_None, nullptr);
#endif
	}

	/**
	 * ImportText，传入 Owner 供容器/对象引用解析；返回是否解析成功（失败时缓冲区通常未完整消费）。
	 * 与 BehaviorTree set_property 等处对 ImportText 返回值的用法一致。
	 */
	static bool ImportTextFromString(const FProperty* Prop, const FString& Value, void* Data, UObject* Owner)
	{
#if NX_UE_HAS_IMPORT_TEXT_DIRECT
		return Prop->ImportText_Direct(*Value, Data, Owner, PPF_None) != nullptr;
#else
		return Prop->ImportText(*Value, Data, PPF_None, Owner) != nullptr;
#endif
	}

	/** 从 FJsonObject 读取字符串数组，跳过空串与非字符串元素。 */
	static void ReadStringArray(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, TArray<FString>& Out)
	{
		if (!Args.IsValid()) { return; }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args->TryGetArrayField(Key, Arr) || !Arr) { return; }
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (!V.IsValid()) { continue; }
			FString S;
			if (V->TryGetString(S) && !S.IsEmpty()) { Out.Add(MoveTemp(S)); }
		}
	}

	/** 取 updates[0] 的 FJsonObject，用于 IsMatched 自动推断 target，失败返回 nullptr。 */
	static TSharedPtr<FJsonObject> PeekFirstUpdate(const TSharedPtr<FJsonObject>& Args)
	{
		if (!Args.IsValid()) { return nullptr; }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args->TryGetArrayField(TEXT("updates"), Arr) || !Arr || Arr->Num() == 0) { return nullptr; }
		const TSharedPtr<FJsonValue>& First = (*Arr)[0];
		if (!First.IsValid() || First->Type != EJson::Object) { return nullptr; }
		return First->AsObject();
	}

	/**
	 * 定位属性、写入新值，同时回显 oldValue 与实际写入后的 actualValue。
	 * 封装 ResolvePropertyWrite + ExportText + ImportText + ExportText 的公共链路，
	 * 供 set_runtime_actor_property / set_runtime_widget_property 等共用，消除重复 8 行样板。
	 *
	 * @param Target       属性所在 UObject
	 * @param PathSegs     路径段数组（已切分，含完整路径）
	 * @param StartIdx     从 PathSegs[StartIdx] 开始解析（通常为 0；Actor 组件前缀时为 1）
	 * @param NewValue     要写入的字符串值
	 * @param OutOldValue  写入前的导出文本
	 * @param OutActualValue 写入后再次导出的实际文本
	 * @param OutError     失败时的错误描述
	 * @return 成功返回 true，失败时 OutError 已填充
	 */
	static bool WritePropertyAndEcho(
		UObject*               Target,
		const TArray<FString>& PathSegs,
		int32                  StartIdx,
		const FString&         NewValue,
		FString&               OutOldValue,
		FString&               OutActualValue,
		FString&               OutError);
};

// ── 跨版本 AssetRegistry 兼容 ──
#if NX_UE_HAS_CLASS_PATHS
	#define NEXUS_FILTER_ADD_CLASS(Filter, Class) (Filter).ClassPaths.Add((Class)->GetClassPathName())
	#define NEXUS_ASSET_CLASS_NAME(AssetData)     (AssetData).AssetClassPath.GetAssetName()
#else
	#define NEXUS_FILTER_ADD_CLASS(Filter, Class) (Filter).ClassNames.Add((Class)->GetFName())
	#define NEXUS_ASSET_CLASS_NAME(AssetData)     (AssetData).AssetClass
#endif
