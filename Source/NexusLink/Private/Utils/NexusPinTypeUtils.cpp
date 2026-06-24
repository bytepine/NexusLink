// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPinTypeUtils.h"

#if WITH_EDITOR
#include "Utils/NexusAssetUtils.h"
#include "EdGraphSchema_K2.h"

bool FNexusPinTypeUtils::ParsePinType(const FString& TypeStr, FEdGraphPinType& OutPinType, FString& OutError)
{
	const FString T = TypeStr.ToLower();
	if      (T == TEXT("bool"))      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	else if (T == TEXT("int"))       OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	else if (T == TEXT("float"))     OutPinType.PinCategory = UEdGraphSchema_K2::PC_Float;
	else if (T == TEXT("string"))    OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	else if (T == TEXT("name"))      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	else if (T == TEXT("text"))      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	else if (T == TEXT("vector"))    { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();    }
	else if (T == TEXT("rotator"))   { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();   }
	else if (T == TEXT("transform")) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get(); }
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
