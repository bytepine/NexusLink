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
	static constexpr TCHAR Loopback[] = TEXT("127.0.0.1");

	/** 列出可写入跨机 url 的网卡 IPv4。 */
	static TArray<FNexusLanIPv4> ListLanIPv4();
};
