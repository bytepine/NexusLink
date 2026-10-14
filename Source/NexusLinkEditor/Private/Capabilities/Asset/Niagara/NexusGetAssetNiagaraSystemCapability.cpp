// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusGetAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
#include "NiagaraParameterStore.h"
#endif
#if WITH_EDITOR
#include "Utils/NexusNiagaraGraphUtils.h"
#endif
#include "NexusMcpTool.h"

void FNexusGetAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetNiagaraSystem");
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
	Out.Description = TEXT("Inspect NiagaraSystem snapshot. Writes via ManageAssetNiagaraSystem.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("NiagaraSystem asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle"), TEXT("emitter"), TEXT("fx") };
	Out.RelatedCapabilities = { TEXT("ManageAssetNiagaraSystem"), TEXT("CreateAssetNiagaraSystem"), TEXT("SearchAsset"), TEXT("GetAssetRefs"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Read Niagara emitters and module stack; use ManageAssetNiagaraSystem for writes");
}

FCapabilityResult FNexusGetAssetNiagaraSystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		UNiagaraSystem* System = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraSystem>(Path);
		if (!System)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), System->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("NiagaraSystem"));

		TArray<TSharedPtr<FJsonValue>> Emitters;
#if NX_UE_HAS_NIAGARA_EMITTER_HANDLES_API
		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		Entry->SetNumberField(TEXT("EmitterCount"), Handles.Num());
		for (const FNiagaraEmitterHandle& Handle : Handles)
		{
			if (!Handle.IsValid()) continue;
			TSharedPtr<FJsonObject> Em = MakeShared<FJsonObject>();
			Em->SetStringField(TEXT("name"), Handle.GetName().ToString());
			Em->SetStringField(TEXT("Id"), Handle.GetId().ToString());
			Em->SetBoolField(TEXT("bEnabled"), Handle.GetIsEnabled());
#if WITH_EDITOR
			{
				TArray<TSharedPtr<FJsonObject>> ModuleObjs;
				FNexusNiagaraGraphUtils::CollectModules(Handle, ModuleObjs);
				TArray<TSharedPtr<FJsonValue>> Modules;
				for (const TSharedPtr<FJsonObject>& M : ModuleObjs)
				{
					Modules.Add(MakeShared<FJsonValueObject>(M));
				}
				Em->SetArrayField(TEXT("Modules"), Modules);
			}
#endif
			Emitters.Add(MakeShared<FJsonValueObject>(Em));
		}
#else
		const int32 NumEmitters = System->GetNumEmitters();
		Entry->SetNumberField(TEXT("EmitterCount"), NumEmitters);
		for (int32 i = 0; i < NumEmitters; ++i)
		{
			const FNiagaraEmitterHandle Handle = System->GetEmitterHandle(i);
			TSharedPtr<FJsonObject> Em = MakeShared<FJsonObject>();
			Em->SetStringField(TEXT("name"), Handle.GetName().ToString());
			Em->SetStringField(TEXT("Id"), Handle.GetId().ToString());
			Em->SetBoolField(TEXT("bEnabled"), Handle.GetIsEnabled());
#if WITH_EDITOR
			{
				TArray<TSharedPtr<FJsonObject>> ModuleObjs;
				FNexusNiagaraGraphUtils::CollectModules(Handle, ModuleObjs);
				TArray<TSharedPtr<FJsonValue>> Modules;
				for (const TSharedPtr<FJsonObject>& M : ModuleObjs)
				{
					Modules.Add(MakeShared<FJsonValueObject>(M));
				}
				Em->SetArrayField(TEXT("Modules"), Modules);
			}
#endif
			Emitters.Add(MakeShared<FJsonValueObject>(Em));
		}
#endif
		Entry->SetArrayField(TEXT("Emitters"), Emitters);

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
		Entry->SetNumberField(TEXT("UserParameterCount"), Store.Num());
		Entry->SetArrayField(TEXT("UserParameters"), Params);
#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetNiagaraSystemCapability)

#endif // WITH_NIAGARA
