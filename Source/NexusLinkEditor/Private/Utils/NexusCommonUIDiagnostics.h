// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NexusCapability.h"
struct FNexusCommonUIDiagnostics final
{
	static void Read(const FString& Section, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Result, FString& Error);
	static FCapabilityResult Control(const TSharedPtr<FJsonObject>& Args);
	static void Shutdown();
};
