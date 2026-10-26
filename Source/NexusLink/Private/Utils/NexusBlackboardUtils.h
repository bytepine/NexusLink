// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/DefaultValueHelper.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"

struct FNexusBlackboardUtils final
{
	static void WriteTyped(UBlackboardComponent* Board, FName Key, const UBlackboardKeyType* Type, FJsonObject& Result)
	{
		const UClass* Class = Type ? Type->GetClass() : nullptr;
		TSharedPtr<FJsonValue> Value;
		FString TypeName;
		if (Class == UBlackboardKeyType_Bool::StaticClass()) { TypeName = TEXT("Bool"); Value = MakeShared<FJsonValueBoolean>(Board->GetValueAsBool(Key)); }
		else if (Class == UBlackboardKeyType_Int::StaticClass()) { TypeName = TEXT("Int"); Value = MakeShared<FJsonValueNumber>(Board->GetValueAsInt(Key)); }
		else if (Class == UBlackboardKeyType_Enum::StaticClass()) { TypeName = TEXT("Enum"); Value = MakeShared<FJsonValueNumber>(Board->GetValueAsEnum(Key)); }
		else if (Class == UBlackboardKeyType_Float::StaticClass())
		{
			TypeName = TEXT("Float");
			const float Number = Board->GetValueAsFloat(Key);
			if (FMath::IsFinite(Number)) Value = MakeShared<FJsonValueNumber>(Number);
		}
		else if (Class == UBlackboardKeyType_String::StaticClass()) { TypeName = TEXT("String"); Value = MakeShared<FJsonValueString>(Board->GetValueAsString(Key)); }
		else if (Class == UBlackboardKeyType_Name::StaticClass()) { TypeName = TEXT("Name"); Value = MakeShared<FJsonValueString>(Board->GetValueAsName(Key).ToString()); }
		else if (Class == UBlackboardKeyType_Object::StaticClass() || Class == UBlackboardKeyType_Class::StaticClass())
		{
			const bool bClass = Class == UBlackboardKeyType_Class::StaticClass();
			TypeName = bClass ? TEXT("Class") : TEXT("Object");
			UObject* Object = bClass ? Board->GetValueAsClass(Key) : Board->GetValueAsObject(Key);
			if (Object) Value = MakeShared<FJsonValueString>(Object->GetPathName());
			else Value = MakeShared<FJsonValueNull>();
		}
		else if (Class == UBlackboardKeyType_Vector::StaticClass() || Class == UBlackboardKeyType_Rotator::StaticClass())
		{
			const bool bVector = Class == UBlackboardKeyType_Vector::StaticClass();
			TypeName = bVector ? TEXT("Vector") : TEXT("Rotator");
			const FVector V = bVector ? Board->GetValueAsVector(Key) : FVector::ZeroVector;
			const FRotator R = bVector ? FRotator::ZeroRotator : Board->GetValueAsRotator(Key);
			if (!V.ContainsNaN() && !R.ContainsNaN())
			{
				TSharedPtr<FJsonObject> Components = MakeShared<FJsonObject>();
				Components->SetNumberField(bVector ? TEXT("X") : TEXT("Pitch"), bVector ? V.X : R.Pitch);
				Components->SetNumberField(bVector ? TEXT("Y") : TEXT("Yaw"), bVector ? V.Y : R.Yaw);
				Components->SetNumberField(bVector ? TEXT("Z") : TEXT("Roll"), bVector ? V.Z : R.Roll);
				Value = MakeShared<FJsonValueObject>(Components);
			}
		}
		Result.SetStringField(TEXT("TypeName"), TypeName.IsEmpty() ? TEXT("Unsupported") : TypeName);
		Result.SetBoolField(TEXT("bTypedValueAvailable"), Value.IsValid());
		if (Value.IsValid()) Result.SetField(TEXT("TypedValue"), Value);
	}

	static bool IsValidScalar(const FString& Type, const FString& Value)
	{
		if (Type.Contains(TEXT("Bool"))) return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0") || Value == TEXT("1");
		if (Type.Contains(TEXT("Int")) || Type.Contains(TEXT("Enum")))
		{
			TCHAR* End = nullptr;
			const int64 Number = FCString::Strtoi64(*Value, &End, 10);
			return !Value.IsEmpty() && End == *Value + Value.Len() && Number >= (Type.Contains(TEXT("Enum")) ? 0 : MIN_int32) && Number <= (Type.Contains(TEXT("Enum")) ? 255 : MAX_int32);
		}
		if (Type.Contains(TEXT("Float")))
		{
			double Number = 0;
			return FDefaultValueHelper::ParseDouble(Value, Number) && FMath::IsFinite(Number) && FMath::IsFinite(static_cast<float>(Number));
		}
		return true;
	}
};
