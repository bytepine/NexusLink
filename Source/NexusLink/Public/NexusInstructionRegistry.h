// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 扩展插件注册的握手说明片段。
 * 主插件不扫描其他插件目录；谁要拼进 initialize.instructions，谁在模块启动时 Register。
 */
class NEXUSLINK_API FNexusInstructionRegistry
{
public:
	static FNexusInstructionRegistry& Get();

	/** 同 SourceName 再次注册会覆盖。两段都空则忽略。 */
	void Register(const FString& SourceName, const FString& SearchMode, const FString& MultiTool);

	void Unregister(const FString& SourceName);

	/** 按 SourceName 排序，把已注册片段接在 Base 后面。 */
	FString Append(const FString& Base, bool bMultiTool) const;

private:
	struct FFragment
	{
		FString SourceName;
		FString SearchMode;
		FString MultiTool;
	};

	TArray<FFragment> Fragments;
};
