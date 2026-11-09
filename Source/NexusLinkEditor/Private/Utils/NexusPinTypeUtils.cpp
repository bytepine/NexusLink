// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPinTypeUtils.h"

#if WITH_EDITOR
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"

static FString PinAtomName(const FName& Category, const FName& Subcategory, UObject* TypeObject)
{
	if (TypeObject) return Category == UEdGraphSchema_K2::PC_Class
		? TEXT("ClassReference<") + TypeObject->GetPathName() + TEXT(">") : TypeObject->GetPathName();
	if (Category == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
	if (Category == UEdGraphSchema_K2::PC_String) return TEXT("string");
#if NX_UE_HAS_K2_PIN_REAL
	if (Category == UEdGraphSchema_K2::PC_Real) return Subcategory.ToString();
#endif
	return Category.ToString();
}

TSharedRef<FJsonObject> FNexusPinTypeUtils::DescribePinType(const FEdGraphPinType& PinType)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("Type"), PinType.PinCategory.ToString());
	if (!PinType.PinSubCategory.IsNone()) Result->SetStringField(TEXT("SubType"), PinType.PinSubCategory.ToString());
	Result->SetBoolField(TEXT("bIsReference"), PinType.bIsReference);
	Result->SetBoolField(TEXT("bIsConst"), PinType.bIsConst);
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		UClass* Class = Cast<UClass>(PinType.PinSubCategoryObject.Get());
		if (Class && Class->IsChildOf(UClass::StaticClass()))
		{
			Result->SetBoolField(TEXT("bUnsafeClassObject"), true);
			Result->SetStringField(TEXT("Warning"), TEXT("UClass object reference can crash reflection consumers; migrate to ClassReference<BaseClass> after reviewing defaults and references"));
		}
	}
	const FString Element = PinAtomName(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
	FString Canonical = Element;
	const TCHAR* Container = TEXT("None");
	if (PinType.IsArray()) { Container = TEXT("Array"); Canonical = TEXT("Array<") + Element + TEXT(">"); }
	else if (PinType.IsSet()) { Container = TEXT("Set"); Canonical = TEXT("Set<") + Element + TEXT(">"); }
	else if (PinType.IsMap())
	{
		Container = TEXT("Map");
		const FString Value = PinAtomName(PinType.PinValueType.TerminalCategory, PinType.PinValueType.TerminalSubCategory, PinType.PinValueType.TerminalSubCategoryObject.Get());
		Result->SetStringField(TEXT("KeyType"), Element);
		Result->SetStringField(TEXT("ValueType"), Value);
		Result->SetStringField(TEXT("ValueTypeCategory"), PinType.PinValueType.TerminalCategory.ToString());
		if (!PinType.PinValueType.TerminalSubCategory.IsNone()) Result->SetStringField(TEXT("ValueSubType"), PinType.PinValueType.TerminalSubCategory.ToString());
		if (PinType.PinValueType.TerminalSubCategoryObject.IsValid()) Result->SetStringField(TEXT("ValueTypeObjectPath"), PinType.PinValueType.TerminalSubCategoryObject->GetPathName());
		Canonical = TEXT("Map<") + Element + TEXT(",") + Value + TEXT(">");
	}
	Result->SetStringField(TEXT("ContainerType"), Container);
	Result->SetStringField(TEXT("ElementType"), Element);
	Result->SetStringField(TEXT("CanonicalType"), Canonical);
	Result->SetBoolField(TEXT("bHashable"), FBlueprintEditorUtils::HasGetTypeHash(PinType));
	if (PinType.PinSubCategoryObject.IsValid()) Result->SetStringField(TEXT("TypeObjectPath"), PinType.PinSubCategoryObject->GetPathName());
	return Result;
}

static bool ParseScalarPinType(const FString& TypeStr, FEdGraphPinType& OutPinType, FString& OutError)
{
	if (TypeStr.StartsWith(TEXT("ClassReference<")) && TypeStr.EndsWith(TEXT(">")))
	{
		const FString ClassName = TypeStr.Mid(15, TypeStr.Len() - 16).TrimStartAndEnd();
		UClass* Class = !ClassName.Contains(TEXT("<")) && !ClassName.Contains(TEXT(">"))
			? FNexusAssetUtils::FindClassWithUPrefix(ClassName) : nullptr;
		if (!Class) { OutError = TEXT("ClassReference requires a valid base class: ") + ClassName; return false; }
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		OutPinType.PinSubCategoryObject = Class;
		return true;
	}
	const FString T = TypeStr.ToLower();
	if      (T == TEXT("bool"))      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	else if (T == TEXT("int"))       OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	else if (T == TEXT("float"))
	{
#if NX_UE_HAS_K2_PIN_REAL
		// UE5 用 real + float 子类型；旧 float 类别会被编译器退化为 int32。
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
#else
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Float;
#endif
	}
	else if (T == TEXT("string"))    OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	else if (T == TEXT("name"))      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	else if (T == TEXT("text"))      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	else if (T == TEXT("vector"))    { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();    }
	else if (T == TEXT("rotator"))   { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();   }
	else if (T == TEXT("transform")) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get(); }
	else
	{
		// 结构体须先于 UClass 解析，避免把 /Script/Module.Struct 错当作对象类加载。
#if NX_UE_HAS_FIND_FIRST_OBJECT
		UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*TypeStr);
#else
		UScriptStruct* Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *TypeStr);
#endif
		if (!Struct && TypeStr.StartsWith(TEXT("/"))) Struct = LoadObject<UScriptStruct>(nullptr, *TypeStr, nullptr, LOAD_NoWarn);
		if (Struct)
		{
			if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
			{ OutError = TEXT("Struct is not a BlueprintType variable type: ") + TypeStr; return false; }
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = Struct;
			return true;
		}
		UClass* Cls = FNexusAssetUtils::FindClassWithUPrefix(TypeStr);
		if (!Cls)
		{
			OutError = FString::Printf(TEXT("Unsupported variable type: %s"), *TypeStr);
			return false;
		}
		if (Cls->IsChildOf(UClass::StaticClass()))
		{
			OutError = TEXT("UClass object pins are unsafe; use ClassReference<BaseClass> (or Array<ClassReference<BaseClass>>) instead");
			return false;
		}
		OutPinType.PinCategory          = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = Cls;
	}
	return true;
}

bool FNexusPinTypeUtils::ParsePinType(const FString& TypeStr, FEdGraphPinType& OutPinType, FString& OutError)
{
	OutPinType = FEdGraphPinType();
	OutError.Reset();
	FString ElementType = TypeStr.TrimStartAndEnd();
	EPinContainerType Container = EPinContainerType::None;
	FString ValueType;
	if (ElementType.RemoveFromEnd(TEXT("[]"))) Container = EPinContainerType::Array;
	else if (ElementType.StartsWith(TEXT("Array<")) || ElementType.StartsWith(TEXT("Set<")) || ElementType.StartsWith(TEXT("Map<")))
	{
		if (!ElementType.EndsWith(TEXT(">"))) { OutError = TEXT("Container type is missing closing >"); return false; }
		const bool bMap = ElementType.StartsWith(TEXT("Map<"));
		Container = bMap ? EPinContainerType::Map : (ElementType.StartsWith(TEXT("Set<")) ? EPinContainerType::Set : EPinContainerType::Array);
		const int32 PrefixLength = Container == EPinContainerType::Array ? 6 : 4;
		ElementType = ElementType.Mid(PrefixLength, ElementType.Len() - PrefixLength - 1);
		if (bMap)
		{
			FString KeyType;
			if (!ElementType.Split(TEXT(","), &KeyType, &ValueType))
			{ OutError = TEXT("Map requires Map<KeyType,ValueType>"); return false; }
			ElementType = KeyType;
		}
	}
	ElementType.TrimStartAndEndInline(); ValueType.TrimStartAndEndInline();
	// 蓝图不支持嵌套容器；先拒绝语法错误，不能递归解析后产生无效 PinType。
	for (const FString& Part : {ElementType, ValueType})
	{
		const bool bClassReference = Part.StartsWith(TEXT("ClassReference<")) && Part.EndsWith(TEXT(">"));
		if ((!bClassReference && (Part.Contains(TEXT("<")) || Part.Contains(TEXT(">")))) || Part.Contains(TEXT("[")) || Part.Contains(TEXT("]")) || Part.Contains(TEXT(",")))
		{ OutError = TEXT("Malformed type or nested containers are unsupported"); return false; }
	}
	FEdGraphPinType Parsed;
	if (ElementType.IsEmpty() || !ParseScalarPinType(ElementType, Parsed, OutError))
	{ if (OutError.IsEmpty()) OutError = TEXT("Element type is required"); return false; }
	if ((Container == EPinContainerType::Map || Container == EPinContainerType::Set) && !FBlueprintEditorUtils::HasGetTypeHash(Parsed))
	{ OutError = TEXT("Map keys and Set elements require a hashable type"); return false; }
	if (Container == EPinContainerType::Map)
	{
		FEdGraphPinType ValuePin;
		if (ValueType.IsEmpty() || !ParseScalarPinType(ValueType, ValuePin, OutError))
		{ if (OutError.IsEmpty()) OutError = TEXT("Map value type is required"); return false; }
		Parsed.PinValueType.TerminalCategory = ValuePin.PinCategory;
		Parsed.PinValueType.TerminalSubCategory = ValuePin.PinSubCategory;
		Parsed.PinValueType.TerminalSubCategoryObject = ValuePin.PinSubCategoryObject;
	}
	Parsed.ContainerType = Container;
	OutPinType = Parsed;
	return true;
}
#endif
