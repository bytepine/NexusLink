// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** 首批标量资产编辑。预览只操作临时属性存储，写入与预览共用解析逻辑。 */
struct FNexusAssetEditUtils final
{
	static bool IsVerified(const TSharedPtr<FJsonObject>& Args);
	static bool ValidateOperations(const FString& Capability, const TSharedPtr<FJsonObject>& Args, FString& Error);
	static void Preview(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation, TSharedPtr<FJsonObject>& Entry);
	static void Apply(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation, bool bVerify, TSharedPtr<FJsonObject>& Entry);
};
