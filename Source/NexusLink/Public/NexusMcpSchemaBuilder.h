// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * MCP 工具 JSON Schema 构造器。
 *
 * 统一所有工具 InputSchema 的构造方式，强制使用 JSON Schema 原生字段
 * （enum / default / minimum / maximum）替代文本化的枚举与默认值，
 * 从源头压缩 tools/list payload 的 token 消耗。
 *
 * 用法示例：
 * @code
 * Def.InputSchema = FNexusSchema::Object()
 *     .Prop(TEXT("target"),  FNexusSchema::Enum(TEXT("Capture target"), { TEXT("editor"), TEXT("viewport"), TEXT("pie") }, TEXT("editor")))
 *     .Prop(TEXT("format"),  FNexusSchema::Enum(TEXT("Image format"),  { TEXT("png"), TEXT("jpg") }, TEXT("png")))
 *     .Prop(TEXT("maxSize"), FNexusSchema::Int(TEXT("Max pixel dimension"), 1920, 0, 8192))
 *     .Prop(TEXT("silent"),  FNexusSchema::Bool(TEXT("Suppress output"), false))
 *     .Required({ TEXT("target") })
 *     .Build();
 * @endcode
 *
 * 描述规范：
 * - 每个参数 description ≤ 50 字符；禁止写默认值、枚举值、范围（走原生字段）
 * - Def.Description ≤ 80 字符，一句话概括
 */
struct FNexusSchema
{
	/**
	 * 顶层对象 Schema 构造器。
	 * 链式调用：FNexusSchema::Object().Prop(...).Prop(...).Required({...}).Build()
	 * Build() 返回可直接赋给 Def.InputSchema 的 TSharedPtr。
	 */
	struct FObjectBuilder
	{
		TSharedPtr<FJsonObject> Schema;
		TSharedPtr<FJsonObject> Properties;
		TArray<TSharedPtr<FJsonValue>> RequiredList;

		FObjectBuilder()
			: Schema(MakeShared<FJsonObject>())
			, Properties(MakeShared<FJsonObject>())
		{
			Schema->SetStringField(TEXT("type"), TEXT("object"));
		}

		FObjectBuilder& Prop(const TCHAR* Name, const TSharedRef<FJsonObject>& P)
		{
			Properties->SetObjectField(Name, P);
			return *this;
		}

		FObjectBuilder& Required(std::initializer_list<const TCHAR*> Names)
		{
			for (const TCHAR* N : Names) { RequiredList.Add(MakeShared<FJsonValueString>(N)); }
			return *this;
		}

		/** 快捷重载：同时添加属性并标记为 required，等价于 .Prop(Name, P).Required({Name})。 */
		FObjectBuilder& Required(const TCHAR* Name, const TSharedRef<FJsonObject>& P)
		{
			Properties->SetObjectField(Name, P);
			RequiredList.Add(MakeShared<FJsonValueString>(Name));
			return *this;
		}

		TSharedPtr<FJsonObject> Build()
		{
			Schema->SetObjectField(TEXT("properties"), Properties);
			if (RequiredList.Num() > 0) { Schema->SetArrayField(TEXT("required"), RequiredList); }
			return Schema;
		}
	};

	/** string 类型属性。 */
	static TSharedRef<FJsonObject> Str(const TCHAR* Desc, const TCHAR* Default = nullptr)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("string"));
		P->SetStringField(TEXT("description"), Desc);
		if (Default) { P->SetStringField(TEXT("default"), Default); }
		return P;
	}

	/** integer 类型属性，可选 default / minimum / maximum。 */
	static TSharedRef<FJsonObject> Int(const TCHAR* Desc,
		int64 Default = TNumericLimits<int64>::Min(),
		int64 Minimum = TNumericLimits<int64>::Min(),
		int64 Maximum = TNumericLimits<int64>::Max())
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("integer"));
		P->SetStringField(TEXT("description"), Desc);
		if (Default != TNumericLimits<int64>::Min()) { P->SetNumberField(TEXT("default"), static_cast<double>(Default)); }
		if (Minimum != TNumericLimits<int64>::Min()) { P->SetNumberField(TEXT("minimum"), static_cast<double>(Minimum)); }
		if (Maximum != TNumericLimits<int64>::Max()) { P->SetNumberField(TEXT("maximum"), static_cast<double>(Maximum)); }
		return P;
	}

	/** boolean 类型属性，可选 default。 */
	static TSharedRef<FJsonObject> Bool(const TCHAR* Desc, bool bHasDefault = false, bool Default = false)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("boolean"));
		P->SetStringField(TEXT("description"), Desc);
		if (bHasDefault) { P->SetBoolField(TEXT("default"), Default); }
		return P;
	}

	/** number（浮点）类型属性。不传 Default 则不写 default 字段。 */
	static TSharedRef<FJsonObject> Num(const TCHAR* Desc)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("number"));
		P->SetStringField(TEXT("description"), Desc);
		return P;
	}

	/** number 带默认值。 */
	static TSharedRef<FJsonObject> Num(const TCHAR* Desc, double Default)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("number"));
		P->SetStringField(TEXT("description"), Desc);
		P->SetNumberField(TEXT("default"), Default);
		return P;
	}

	/** 枚举属性（string + enum）。Default 可为 nullptr。 */
	static TSharedRef<FJsonObject> Enum(const TCHAR* Desc,
		std::initializer_list<const TCHAR*> Values,
		const TCHAR* Default = nullptr)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("string"));
		P->SetStringField(TEXT("description"), Desc);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(static_cast<int32>(Values.size()));
		for (const TCHAR* V : Values) { Arr.Add(MakeShared<FJsonValueString>(V)); }
		P->SetArrayField(TEXT("enum"), Arr);
		if (Default) { P->SetStringField(TEXT("default"), Default); }
		return P;
	}

	/** string[] 枚举数组：array + items.enum，用于 multi-section 的 sections 字段。Values 应已包含 "all"。 */
	static TSharedRef<FJsonObject> EnumArr(const TCHAR* Desc, const TArray<FString>& Values)
	{
		TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
		Items->SetStringField(TEXT("type"), TEXT("string"));
		TArray<TSharedPtr<FJsonValue>> EnumArr;
		for (const FString& V : Values) { EnumArr.Add(MakeShared<FJsonValueString>(V)); }
		Items->SetArrayField(TEXT("enum"), EnumArr);

		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("array"));
		P->SetStringField(TEXT("description"), Desc);
		P->SetObjectField(TEXT("items"), Items);
		return P;
	}

	/** string[] 数组。 */
	static TSharedRef<FJsonObject> StrArr(const TCHAR* Desc)
	{
		TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
		Items->SetStringField(TEXT("type"), TEXT("string"));

		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("array"));
		P->SetStringField(TEXT("description"), Desc);
		P->SetObjectField(TEXT("items"), Items);
		return P;
	}

	/** 内嵌对象（自定义 items schema）。调用方传入已构造好的 item object schema。 */
	static TSharedRef<FJsonObject> ArrayOf(const TCHAR* Desc, const TSharedRef<FJsonObject>& ItemSchema)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("array"));
		P->SetStringField(TEXT("description"), Desc);
		P->SetObjectField(TEXT("items"), ItemSchema);
		return P;
	}

	/** 通用 object（无 properties 限制）。 */
	static TSharedRef<FJsonObject> AnyObject(const TCHAR* Desc)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("object"));
		P->SetStringField(TEXT("description"), Desc);
		return P;
	}

	static FObjectBuilder Object() { return FObjectBuilder(); }

	/** 空 schema：{ "type": "object" }，用于无参数工具（Dispatcher 已会兜底，这里仅显式化）。 */
	static TSharedPtr<FJsonObject> EmptyObject()
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("object"));
		return P;
	}
};
