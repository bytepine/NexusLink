// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Utils/NexusPinTypeUtils.h"
#include "Utils/NexusVersionCompat.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkPinTypeUtilsTest,
	"NexusLink.Utils.PinType.ParsePrimitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkPinTypeUtilsTest::RunTest(const FString& Parameters)
{
	// 基本类型 —— 大小写不敏感、全部能解析
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("bool"),
			FNexusPinTypeUtils::ParsePinType(TEXT("bool"), Pin, Err));
		TestEqual(TEXT("bool category"),
			Pin.PinCategory, UEdGraphSchema_K2::PC_Boolean);
	}
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("INT uppercase"),
			FNexusPinTypeUtils::ParsePinType(TEXT("INT"), Pin, Err));
		TestEqual(TEXT("int category"),
			Pin.PinCategory, UEdGraphSchema_K2::PC_Int);
	}
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("float"),
			FNexusPinTypeUtils::ParsePinType(TEXT("float"), Pin, Err));
		// UE 5 double-precision pin uses PC_Real; UE 4 only has PC_Float (PC_Real 符号缺失).
		const bool bIsFloat = Pin.PinCategory == UEdGraphSchema_K2::PC_Float;
#if NX_UE_HAS_K2_PIN_REAL
		const bool bIsReal = Pin.PinCategory == UEdGraphSchema_K2::PC_Real;
		TestTrue(TEXT("float maps to PC_Real or PC_Float"), bIsReal || bIsFloat);
#else
		TestTrue(TEXT("float maps to PC_Float (UE4)"), bIsFloat);
#endif
	}
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("string"),
			FNexusPinTypeUtils::ParsePinType(TEXT("string"), Pin, Err));
		TestEqual(TEXT("string category"),
			Pin.PinCategory, UEdGraphSchema_K2::PC_String);
	}
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("name"),
			FNexusPinTypeUtils::ParsePinType(TEXT("name"), Pin, Err));
		TestEqual(TEXT("name category"),
			Pin.PinCategory, UEdGraphSchema_K2::PC_Name);
	}
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("text"),
			FNexusPinTypeUtils::ParsePinType(TEXT("text"), Pin, Err));
		TestEqual(TEXT("text category"),
			Pin.PinCategory, UEdGraphSchema_K2::PC_Text);
	}

	// 常用结构体
	{
		FEdGraphPinType Pin; FString Err;
		TestTrue(TEXT("vector"),
			FNexusPinTypeUtils::ParsePinType(TEXT("vector"), Pin, Err));
		TestEqual(TEXT("vector category"),
			Pin.PinCategory, UEdGraphSchema_K2::PC_Struct);
	}

	// 失败路径
	{
		FEdGraphPinType Pin; FString Err;
		const bool bOk = FNexusPinTypeUtils::ParsePinType(
			TEXT("__absolutely_not_a_real_class__"), Pin, Err);
		TestFalse(TEXT("garbage class should fail"), bOk);
		TestFalse(TEXT("error message non-empty on failure"), Err.IsEmpty());
	}

	return true;
}

#endif // WITH_EDITOR
