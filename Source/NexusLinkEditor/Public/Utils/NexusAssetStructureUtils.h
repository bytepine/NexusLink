// Copyright byteyang. All Rights Reserved.
#pragma once

struct FNexusArgs;
#include "NexusActionCapability.h"

/** 有界结构预览与语义回读；不承诺资产落盘回滚。 */
struct NEXUSLINKEDITOR_API FNexusAssetStructureUtils final
{
	using FSnapshotPCG = TFunction<void(UObject*, TFunctionRef<FString(const FString&)>, TArray<FString>&)>;
	using FApplyPCG = TFunction<TOptional<bool>(UObject*, const FString&, const FNexusArgs&, const TSharedPtr<FJsonObject>&, FString&)>;
	static FSnapshotPCG SnapshotPCG;
	static FApplyPCG ApplyPCG;
	static TArray<FString> Snapshot(UObject* Asset, const FString& NormalizeNode = FString());
	static FString Hash(UObject* Asset);
	static bool Check(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Args, FString& Error);
	static bool Supports(const FString& Capability, const FString& Action);
	static bool ApplyPreview(UObject* Copy, const FString& Capability, const TSharedPtr<FJsonObject>& Op, const TSharedPtr<FJsonObject>& Entry, FString& Error, UObject* Source);
	static UObject* Copy(UObject* Asset);
	static void Wrap(const FString& Capability, TMap<FString, FNexusActionHandler>& Handlers, TFunction<UObject*(FNexusActionContext&)> Target);
};
