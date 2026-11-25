// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"

/** 本服务注入的输入按操作拥有，不停止其他来源的 Action 注入。 */
class ULocalPlayer;

struct NEXUSLINK_API FNexusRuntimeInputUtils final
{
	using FResolveAction = TFunction<UObject*(ULocalPlayer*, const FString&, const FVector&, FString&)>;
	using FStartAction = TFunction<void(ULocalPlayer*, UObject*, const FVector&)>;
	using FStopAction = TFunction<void(ULocalPlayer*, UObject*)>;
	using FIsActionActive = TFunction<bool(ULocalPlayer*, UObject*)>;

	static FResolveAction ResolveAction;
	static FStartAction StartAction;
	static FStopAction StopAction;
	static FIsActionActive IsActionActive;

	static FCapabilityResult Apply(const TSharedPtr<FJsonObject>& Arguments);
	static FCapabilityResult Query(const TSharedPtr<FJsonObject>& Arguments, bool bRelease);
	static void Shutdown();
};
