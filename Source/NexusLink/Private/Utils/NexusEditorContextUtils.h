// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/**
 * 编辑器只读上下文：关卡选中 Actor、Content Browser 选中资产与当前路径。
 * editor World ≠ PIE；PIE 选中请用 list_runtime_actors / get_runtime_actor_property。
 */
class FNexusEditorContextUtils final
{
public:
	FNexusEditorContextUtils() = delete;

	static void CollectSelectionActors(TArray<TSharedPtr<FJsonValue>>& OutActors, int32& OutCount, int32 Limit);
	static void CollectSelectionAssets(TArray<TSharedPtr<FJsonValue>>& OutAssets, int32& OutCount, int32 Limit);
	static bool CollectContentBrowserPath(FString& OutPath, FString& OutError);
};
