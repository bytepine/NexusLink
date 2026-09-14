// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 按 GameplayTag 查询引用该标签的资产包路径（AssetRegistry SearchableName）。 */
class FNexusGameplayTagReferencerUtils final
{
public:
	FNexusGameplayTagReferencerUtils() = delete;

	static bool FindReferencerPackagePaths(const FString& TagName, TArray<FString>& OutPaths, FString& OutError);
};
