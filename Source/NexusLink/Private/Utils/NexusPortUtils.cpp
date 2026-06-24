// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPortUtils.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

bool FNexusPortUtils::IsPortInUse(int32 Port)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		// 无法获取子系统，保守返回未占用
		return false;
	}

	FSocket* TestSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("NexusPortTest"), false);
	if (!TestSocket)
	{
		return false;
	}

	TestSocket->SetReuseAddr(false);

	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	Addr->SetAnyAddress();
	Addr->SetPort(Port);

	const bool bBound = TestSocket->Bind(*Addr);

	// 无论绑定结果如何，立即销毁测试 Socket，释放端口
	TestSocket->Close();
	SocketSubsystem->DestroySocket(TestSocket);

	// 绑定成功 → 端口空闲；绑定失败 → 端口已被占用
	return !bBound;
}

int32 FNexusPortUtils::FindAvailablePort(int32 StartPort, const TArray<int32>& ExcludePorts, int32 MaxAttempts)
{
	for (int32 i = 0; i < MaxAttempts; ++i)
	{
		const int32 Candidate = StartPort + i;
		// 端口号最大 65535
		if (Candidate > 65535)
		{
			break;
		}
		// 跳过已分配给其他服务的端口（避免两次独立查找拿到同一空闲端口）
		if (ExcludePorts.Contains(Candidate))
		{
			continue;
		}
		if (!FNexusPortUtils::IsPortInUse(Candidate))
		{
			return Candidate;
		}
	}
	return -1;
}

#if WITH_EDITOR

void FNexusPortUtils::OpenSettingsPanel()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->ShowViewer(TEXT("Editor"), TEXT("Plugins"), TEXT("NexusLink"));
	}
}

#endif // WITH_EDITOR
