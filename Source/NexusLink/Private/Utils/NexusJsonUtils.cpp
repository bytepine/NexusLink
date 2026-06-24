// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusJsonUtils.h"
#include "Math/UnrealMathUtility.h"

FString FNexusJsonUtils::GetStringSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& Default)
{
	if (!Obj.IsValid()) return Default;
	FString Val;
	if (Obj->TryGetStringField(Key, Val)) return Val;
	return Default;
}

int32 FNexusJsonUtils::GetIntSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32 Default, int32 ClampMin, int32 ClampMax)
{
	if (!Obj.IsValid()) return FMath::Clamp(Default, ClampMin, ClampMax);
	double Num = 0.0;
	if (Obj->TryGetNumberField(Key, Num))
		return FMath::Clamp(static_cast<int32>(Num), ClampMin, ClampMax);
	return FMath::Clamp(Default, ClampMin, ClampMax);
}

float FNexusJsonUtils::GetFloatSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float Default)
{
	if (!Obj.IsValid()) return Default;
	double Num = 0.0;
	if (Obj->TryGetNumberField(Key, Num))
		return static_cast<float>(Num);
	return Default;
}

bool FNexusJsonUtils::GetBoolSafe(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Default)
{
	if (!Obj.IsValid()) return Default;
	bool Val = Default;
	if (Obj->TryGetBoolField(Key, Val)) return Val;
	return Default;
}

TArray<FString> FNexusJsonUtils::GetStringArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
{
	TArray<FString> Result;
	if (!Obj.IsValid()) return Result;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj->TryGetArrayField(Key, Arr) || !Arr) return Result;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
			Result.Add(S);
	}
	return Result;
}

bool FNexusJsonUtils::HasSectionAll(const TSharedPtr<FJsonObject>& Obj)
{
	if (!Obj.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj->TryGetArrayField(TEXT("sections"), Arr) || !Arr) return false;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S) && S.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			return true;
	}
	return false;
}

bool FNexusJsonUtils::HasSubSection(const TSharedPtr<FJsonObject>& Obj)
{
	if (!Obj.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj->TryGetArrayField(TEXT("sections"), Arr) || !Arr || Arr->Num() == 0) return false;
	return !HasSectionAll(Obj);
}

void FNexusJsonUtils::ParseOffsetLimit(const TSharedPtr<FJsonObject>& Obj,
                                       int32& OutOffset, int32& OutLimit,
                                       int32 DefaultLimit, int32 MaxLimit)
{
	OutOffset = 0;
	OutLimit  = DefaultLimit;
	if (!Obj.IsValid()) return;
	double Num = 0.0;
	if (Obj->TryGetNumberField(TEXT("offset"), Num))
		OutOffset = FMath::Max(0, static_cast<int32>(Num));
	if (Obj->TryGetNumberField(TEXT("limit"), Num))
		OutLimit = FMath::Clamp(static_cast<int32>(Num), 1, MaxLimit);
}

void FNexusJsonUtils::ComputeSlice(int32 Total, int32 Offset, int32 Limit,
                                   int32& OutStart, int32& OutEnd)
{
	OutStart = FMath::Min(Offset, Total);
	OutEnd   = FMath::Min(OutStart + Limit, Total);
}
