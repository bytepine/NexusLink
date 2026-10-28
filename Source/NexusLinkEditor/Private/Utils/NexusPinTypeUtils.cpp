// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPinTypeUtils.h"

#if WITH_EDITOR
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "EdGraphSchema_K2.h"

bool FNexusPinTypeUtils::ParsePinType(const FString& TypeStr, FEdGraphPinType& OutPinType, FString& OutError)
{
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
	else if (T == TEXT("Vector"))    { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();    }
	else if (T == TEXT("Rotator"))   { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();   }
	else if (T == TEXT("Transform")) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get(); }
	else
	{
		UClass* Cls = FNexusAssetUtils::FindClassWithUPrefix(TypeStr);
		if (!Cls)
		{
			OutError = FString::Printf(TEXT("Unsupported variable type: %s"), *TypeStr);
			return false;
		}
		OutPinType.PinCategory          = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = Cls;
	}
	return true;
}
#endif
