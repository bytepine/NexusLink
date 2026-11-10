// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class ULevel;
class FJsonObject;
class AActor;
class UClass;

/** 编辑器关卡（UWorld 包）只读快照；实现依赖 WITH_EDITOR。 */
class NEXUSLINKEDITOR_API FNexusEditorLevelUtils
{
public:
	/** 将资产路径规范为包路径（去掉 .ObjectName 后缀）。 */
	static FString NormalizeLevelPackagePath(const FString& AssetPath);

	/**
	 * 加载用于只读检查的资产 UWorld，优先复用当前编辑器主关卡和已加载流送子关卡。
	 * @param bOutIsEditorWorld 为 true 表示对应 ULevel 已在编辑器世界中加载（可能含未保存改动）
	 */
	static UWorld* LoadLevelWorldForRead(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError);

	/** 加载用于写入的 UWorld（与 LoadLevelWorldForRead 相同策略）。 */
	static UWorld* LoadLevelWorldForWrite(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError);

	/** 按资产包定位实际 ULevel；流送关卡仍严格限定在自身包内。 */
	static ULevel* ResolveLevel(UWorld* World);

	/** 按 Actor 名或 Editor Label 查找关卡内 Actor。 */
	static AActor* FindLevelActorByNameOrLabel(UWorld* World, const FString& ActorNameOrLabel, bool bAllowLabel = true);

	/** 通知编辑器刷新组件变换、包围盒及导航；由写入方先记录 Undo。 */
	static void RefreshActorAfterPropertyEdit(AActor* Actor);

	/** 在关卡持久层生成 Actor。 */
	static bool SpawnActorInLevelWorld(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation,
		AActor*& OutActor, FString& OutError);

	/** 从关卡移除 Actor。 */
	static bool RemoveLevelActor(UWorld* World, AActor* Actor, FString& OutError);

	/** Sections=actors：分页 Actor 列表写入 Entry。 */
	static void AppendLevelActorsSection(UWorld* World, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry);

	/** Sections=settings：WorldSettings 白名单字段写入 Entry。 */
	static void AppendLevelSettingsSection(UWorld* World, TSharedPtr<FJsonObject>& Entry);
};
