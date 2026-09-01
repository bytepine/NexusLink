// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 一块已启用、非 loopback、非 169.254 的 IPv4。 */
struct FNexusLanIPv4
{
	FString Name;
	FString Address;
};

struct FNexusLanHost
{
	/** C++14 下 constexpr 数组 ODR-use 需类外定义；Clang/ld（Mac）会报 Undefined symbols，MSVC 常放过。 */
	static const TCHAR* Loopback;

	/** 列出可写入跨机 url 的网卡 IPv4。 */
	static TArray<FNexusLanIPv4> ListLanIPv4();
};
