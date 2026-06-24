// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class FJsonObject;
class AActor;
class UClass;

/** 编辑器关卡（UWorld 包）只读快照；实现依赖 WITH_EDITOR。 */
class NEXUSLINK_API FNexusEditorLevelUtils
{
public:
	/** 将资产路径规范为包路径（去掉 .ObjectName 后缀）。 */
	static FString NormalizeLevelPackagePath(const FString& AssetPath);

	/**
	 * 加载用于只读检查的 UWorld：若与当前编辑器已打开关卡同包则复用 Editor World，否则 LoadPackage。
	 * @param bOutIsEditorWorld 为 true 表示返回的是 GEditor 当前 World（可能含未保存改动）
	 */
	static UWorld* LoadLevelWorldForRead(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError);

	/** 加载用于写入的 UWorld（与 LoadLevelWorldForRead 相同策略）。 */
	static UWorld* LoadLevelWorldForWrite(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError);

	/** 按 Actor 名或 Editor Label 查找关卡内 Actor。 */
	static AActor* FindLevelActorByNameOrLabel(UWorld* World, const FString& ActorNameOrLabel);

	/** 在关卡持久层生成 Actor。 */
	static bool SpawnActorInLevelWorld(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation,
		AActor*& OutActor, FString& OutError);

	/** 从关卡移除 Actor。 */
	static bool RemoveLevelActor(UWorld* World, AActor* Actor, FString& OutError);

	/** sections=actors：分页 Actor 列表写入 Entry。 */
	static void AppendLevelActorsSection(UWorld* World, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry);

	/** sections=settings：WorldSettings 白名单字段写入 Entry。 */
	static void AppendLevelSettingsSection(UWorld* World, TSharedPtr<FJsonObject>& Entry);
};
