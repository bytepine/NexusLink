// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** 旧式 Skeleton 重定向；动画引用替换与序列递归重定向均需显式指定。 */
struct FNexusAnimationRetargetUtils final
{
	static void ExtendSchema(const TSharedPtr<FJsonObject>& OperationSchema);
	static bool Apply(UObject* Asset, const TSharedPtr<FJsonObject>& Operation, const TSharedPtr<FJsonObject>& Entry, bool bFinalizeRequested);
};
