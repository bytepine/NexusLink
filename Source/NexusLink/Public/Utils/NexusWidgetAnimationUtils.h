// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
class UWidgetAnimation;
class UMovieScene;
class FJsonObject;

/** WidgetBlueprint 动画（UWidgetAnimation + MovieScene）增删轨/关键帧。§7.4 豁免。 */
class NEXUSLINK_API FNexusWidgetAnimationUtils
{
public:
	/** 按对象名或 DisplayLabel 查找动画。 */
	static UWidgetAnimation* FindAnimation(UWidgetBlueprint* WBP, const FString& Name);

	/** 新建空动画并挂到 WBP->Animations。失败时 OutError 非空。 */
	static UWidgetAnimation* AddAnimation(UWidgetBlueprint* WBP, const FString& Name, FString& OutError);

	/** 从 WBP 移除动画。 */
	static bool RemoveAnimation(UWidgetBlueprint* WBP, const FString& Name, FString& OutError);

	/** 向动画 MovieScene 添加 Float 轨（Master/Root）。返回轨显示名。 */
	static bool AddFloatTrack(UWidgetAnimation* Anim, const FString& TrackName, FString& OutTrackName, FString& OutError);

	/**
	 * 绑定控件 Float 属性轨（如 RenderOpacity）。
	 * 创建/复用 Possessable + FWidgetAnimationBinding，再 AddTrack 到该 Binding。
	 */
	static bool AddBoundFloatTrack(UWidgetAnimation* Anim, UWidgetBlueprint* WBP,
		const FString& WidgetName, const FString& PropertyPath,
		const FString& TrackName, FString& OutTrackName, FString& OutError);

	/** 在指定轨（空则首条 Float）写入一帧关键帧（秒）。 */
	static bool AddFloatKey(UWidgetAnimation* Anim, float TimeSeconds, float Value, FString& OutError);
	static bool AddFloatKey(UWidgetAnimation* Anim, const FString& TrackName, float TimeSeconds, float Value, FString& OutError);

	/** 按显示名移除 Float 轨。 */
	static bool RemoveFloatTrack(UWidgetAnimation* Anim, const FString& TrackName, FString& OutError);

	/** 按时间移除关键帧（空 TrackName 则首条 Float 轨）。 */
	static bool RemoveFloatKey(UWidgetAnimation* Anim, const FString& TrackName, float TimeSeconds, FString& OutError);

	/** 序列化动画轨摘要到 JSON（trackClass / displayName / sectionsCount）。 */
	static void AppendTrackSummaries(UWidgetAnimation* Anim, TArray<TSharedPtr<class FJsonValue>>& OutTracks);
};
