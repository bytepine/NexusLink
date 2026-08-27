// Copyright byteyang. All Rights Reserved.

#include "NexusLanHost.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <iphlpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

static bool IsLinkLocalIPv4(const FString& Address)
{
	return Address.StartsWith(TEXT("169.254."));
}

static void AddUniqueLan(TArray<FNexusLanIPv4>& Out, const FString& Name, const FString& Address)
{
	if (Address.IsEmpty() || Address == FNexusLanHost::Loopback || IsLinkLocalIPv4(Address))
	{
		return;
	}
	for (const FNexusLanIPv4& E : Out)
	{
		if (E.Address == Address)
		{
			return;
		}
	}
	FNexusLanIPv4 Entry;
	Entry.Name = Name;
	Entry.Address = Address;
	Out.Add(MoveTemp(Entry));
}

TArray<FNexusLanIPv4> FNexusLanHost::ListLanIPv4()
{
	TArray<FNexusLanIPv4> Out;

#if PLATFORM_WINDOWS
	ULONG Flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG BufLen = 15000;
	PIP_ADAPTER_ADDRESSES Adapters = static_cast<PIP_ADAPTER_ADDRESSES>(FMemory::Malloc(BufLen));
	ULONG Ret = GetAdaptersAddresses(AF_INET, Flags, nullptr, Adapters, &BufLen);
	if (Ret == ERROR_BUFFER_OVERFLOW)
	{
		FMemory::Free(Adapters);
		Adapters = static_cast<PIP_ADAPTER_ADDRESSES>(FMemory::Malloc(BufLen));
		Ret = GetAdaptersAddresses(AF_INET, Flags, nullptr, Adapters, &BufLen);
	}
	if (Ret == NO_ERROR && Adapters)
	{
		for (PIP_ADAPTER_ADDRESSES Adapter = Adapters; Adapter; Adapter = Adapter->Next)
		{
			if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			{
				continue;
			}
			const FString Name = Adapter->FriendlyName ? FString(Adapter->FriendlyName) : FString(Adapter->AdapterName);
			for (PIP_ADAPTER_UNICAST_ADDRESS Uni = Adapter->FirstUnicastAddress; Uni; Uni = Uni->Next)
			{
				if (!Uni->Address.lpSockaddr || Uni->Address.lpSockaddr->sa_family != AF_INET)
				{
					continue;
				}
				const sockaddr_in* Sin = reinterpret_cast<const sockaddr_in*>(Uni->Address.lpSockaddr);
				const uint8* B = reinterpret_cast<const uint8*>(&Sin->sin_addr);
				const FString Address = FString::Printf(TEXT("%u.%u.%u.%u"), B[0], B[1], B[2], B[3]);
				AddUniqueLan(Out, Name, Address);
			}
		}
	}
	FMemory::Free(Adapters);
#else
	ifaddrs* Ifaces = nullptr;
	if (getifaddrs(&Ifaces) == 0)
	{
		for (ifaddrs* Iface = Ifaces; Iface; Iface = Iface->ifa_next)
		{
			if (!Iface->ifa_addr || Iface->ifa_addr->sa_family != AF_INET)
			{
				continue;
			}
			const sockaddr_in* Sin = reinterpret_cast<const sockaddr_in*>(Iface->ifa_addr);
			char Buf[INET_ADDRSTRLEN] = {};
			if (!inet_ntop(AF_INET, &Sin->sin_addr, Buf, INET_ADDRSTRLEN))
			{
				continue;
			}
			AddUniqueLan(Out, UTF8_TO_TCHAR(Iface->ifa_name), UTF8_TO_TCHAR(Buf));
		}
		freeifaddrs(Ifaces);
	}
#endif

	return Out;
}
