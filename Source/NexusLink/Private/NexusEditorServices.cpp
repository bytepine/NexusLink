// Copyright byteyang. All Rights Reserved.

#include "NexusEditorServices.h"
#include "NexusCapability.h"
#include "UObject/Package.h"

// 默认降级实现：未安装真实钩子时（独立 Game/DS 包、或 NexusLinkEditor 尚未启动）一律安全跳过。
// 全部定义在同一 TU 内按声明顺序初始化，不依赖其他 TU 的静态初始化顺序。

TFunction<void(const FString&, bool, bool, TSharedPtr<FJsonObject>&)> FNexusEditorServices::ApplyManageFinalize =
	[](const FString&, bool, bool, TSharedPtr<FJsonObject>&) {};

TFunction<TUniquePtr<INexusTransactionHandle>(const FString&)> FNexusEditorServices::BeginTransaction =
	[](const FString&) -> TUniquePtr<INexusTransactionHandle> { return nullptr; };

TFunction<bool()> FNexusEditorServices::IsTransactionActive =
	[]() { return false; };

TFunction<int32()> FNexusEditorServices::GetActiveTransactionRecordCount =
	[]() { return 0; };

TFunction<FCapabilityResult(const FString&, const FString&, const FString&)> FNexusEditorServices::PromptDangerousCapConfirm =
	[](const FString&, const FString&, const FString&)
	{
		return FCapabilityResult::MakeUserDenied(
			TEXT("No editor UI to confirm this dangerous capability. Do not retry."));
	};

TFunction<int32(const TArray<UPackage*>&, bool, bool, TSet<UPackage*>*)> FNexusEditorServices::FlushPackages =
	[](const TArray<UPackage*>&, bool, bool, TSet<UPackage*>*) { return 0; };

TFunction<void()> FNexusEditorServices::OpenSettingsPanel =
	[]() {};

TFunction<void(int32, int32)> FNexusEditorServices::NotifyServerStarted =
	[](int32, int32) {};

TFunction<void()> FNexusEditorServices::NotifyServerStopped =
	[]() {};
