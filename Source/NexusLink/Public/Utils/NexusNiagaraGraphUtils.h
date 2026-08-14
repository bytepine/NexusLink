// Copyright byteyang. All Rights Reserved.

#pragma once

#if WITH_NIAGARA && WITH_EDITOR

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

class UNiagaraSystem;
class UNiagaraEmitter;
class UNiagaraScriptSource;
struct FNiagaraEmitterHandle;
class FJsonObject;

/** Niagara 发射器图 / 模块栈辅助（Editor-only）。不调用 NiagaraEditor 未导出的 StackGraphUtilities。 */
class NEXUSLINK_API FNexusNiagaraGraphUtils
{
public:
	/** 空白发射器（无默认模块/Renderer），Outer 一般为 TransientPackage。 */
	static UNiagaraEmitter* CreateEmptyEmitter(UObject* Outer, FName Name, FString& OutError);

	static UNiagaraScriptSource* GetScriptSource(const FNiagaraEmitterHandle& Handle);

	static ENiagaraScriptUsage ParseUsage(const FString& Usage);

	static FString UsageToString(ENiagaraScriptUsage Usage);

	/** 经 UNiagaraScriptSource::AddModuleIfMissing 挂模块。 */
	static bool AddModule(UNiagaraSystem* System, const FString& EmitterName,
		const FString& ModulePath, const FString& Usage, FString& OutModuleName, FString& OutError);

	static bool RemoveModule(UNiagaraSystem* System, const FString& EmitterName,
		const FString& ModuleName, FString& OutError);

	/** 列出发射器图上的 FunctionCall 模块。 */
	static void CollectModules(const FNiagaraEmitterHandle& Handle, TArray<TSharedPtr<FJsonObject>>& OutModules);
};

#endif // WITH_NIAGARA && WITH_EDITOR
