// Copyright byteyang. All Rights Reserved.

#pragma once

#include "Utils/NexusJsonUtils.h"

/**
 * Capability / Tool 参数读取门面（header-only）。
 *
 * required 与类型校验仅由 FNexusCapability::Run() 按 InputSchema 执行；
 * Execute 内用本门面取值即可，勿再手写 HasField / 空串必填检查。
 * Args 为空指针时所有读取返回默认值。
 */
struct FNexusArgs
{
	explicit FNexusArgs(const TSharedPtr<FJsonObject>& InObj)
		: Obj(InObj)
	{
	}

	FString Str(const TCHAR* Key, const FString& Default = FString()) const
	{
		return FNexusJsonUtils::GetStringSafe(Obj, Key, Default);
	}

	double Num(const TCHAR* Key, double Default = 0.0) const
	{
		if (!Obj.IsValid()) return Default;
		double Val = Default;
		return Obj->TryGetNumberField(Key, Val) ? Val : Default;
	}

	bool Bool(const TCHAR* Key, bool Default = false) const
	{
		return FNexusJsonUtils::GetBoolSafe(Obj, Key, Default);
	}

	TArray<FString> StrArr(const TCHAR* Key) const
	{
		return FNexusJsonUtils::GetStringArray(Obj, Key);
	}

	/** 按 ValidNames 大小写不敏感匹配，命中返回下标，否则 Default。 */
	int32 EnumInt(const TCHAR* Key, const TArray<FString>& ValidNames, int32 Default) const
	{
		const FString S = Str(Key);
		if (S.IsEmpty()) return Default;
		for (int32 i = 0; i < ValidNames.Num(); ++i)
		{
			if (ValidNames[i].Equals(S, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return Default;
	}

	const TSharedPtr<FJsonObject>& GetObj() const { return Obj; }

private:
	TSharedPtr<FJsonObject> Obj;
};
