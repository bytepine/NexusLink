// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusGetAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
#include "NiagaraParameterStore.h"
#endif
#include "NexusMcpTool.h"

void FGetAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_niagara_system");
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
	Out.Description = TEXT("检查 NiagaraSystem 快照。发射器/用户参数。写用 manage_asset_niagara_system。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("NiagaraSystem 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 NiagaraSystem 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle"), TEXT("emitter"), TEXT("fx") };
	Out.RelatedCapabilities = { TEXT("manage_asset_niagara_system"), TEXT("search_asset"), TEXT("get_asset_refs"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读 Niagara 元数据；属性写用 manage_asset_niagara_system");
}

static void CollectNiagaraPaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		OutPaths.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
			{
				OutPaths.AddUnique(P);
			}
		}
	}
}

FCapabilityResult FGetAssetNiagaraSystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectNiagaraPaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), Path);

			UNiagaraSystem* System = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraSystem>(Path);
			if (!System)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), System->GetName());
			Entry->SetStringField(TEXT("assetType"), TEXT("NiagaraSystem"));

			TArray<TSharedPtr<FJsonValue>> Emitters;
#if NX_UE_HAS_NIAGARA_EMITTER_HANDLES_API
			const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
			Entry->SetNumberField(TEXT("emitterCount"), Handles.Num());
			for (const FNiagaraEmitterHandle& Handle : Handles)
			{
				if (!Handle.IsValid()) continue;
				TSharedPtr<FJsonObject> Em = MakeShared<FJsonObject>();
				Em->SetStringField(TEXT("name"), Handle.GetName().ToString());
				Em->SetStringField(TEXT("id"), Handle.GetId().ToString());
				Emitters.Add(MakeShared<FJsonValueObject>(Em));
			}
#else
			const int32 NumEmitters = System->GetNumEmitters();
			Entry->SetNumberField(TEXT("emitterCount"), NumEmitters);
			for (int32 i = 0; i < NumEmitters; ++i)
			{
				const FNiagaraEmitterHandle Handle = System->GetEmitterHandle(i);
				TSharedPtr<FJsonObject> Em = MakeShared<FJsonObject>();
				Em->SetStringField(TEXT("name"), Handle.GetName().ToString());
				Em->SetStringField(TEXT("id"), Handle.GetId().ToString());
				Emitters.Add(MakeShared<FJsonValueObject>(Em));
			}
#endif
			Entry->SetArrayField(TEXT("emitters"), Emitters);

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
			TArray<TSharedPtr<FJsonValue>> Params;
			const FNiagaraParameterStore& Store = System->GetExposedParameters();
			int32 ParamIdx = 0;
			for (const FNiagaraVariableWithOffset& Var : Store.ReadParameterVariables())
			{
				if (ParamIdx >= 32) break;
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), Var.GetName().ToString());
				P->SetStringField(TEXT("type"), Var.GetType().GetName());
				Params.Add(MakeShared<FJsonValueObject>(P));
				++ParamIdx;
			}
			Entry->SetNumberField(TEXT("userParameterCount"), Store.Num());
			Entry->SetArrayField(TEXT("userParameters"), Params);
#endif

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetNiagaraSystemCapability)

#endif // WITH_NIAGARA
