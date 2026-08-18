// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusNiagaraGraphUtils.h"

#if WITH_NIAGARA && WITH_EDITOR

#include "Utils/NexusVersionCompat.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraTypes.h"
#include "NiagaraEditorCommon.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

static FNiagaraEmitterHandle* FindHandleByName(UNiagaraSystem* System, const FString& EmitterName, int32& OutIdx)
{
	OutIdx = INDEX_NONE;
	if (!System || EmitterName.IsEmpty()) return nullptr;
	const int32 NumEm =
#if NX_UE_HAS_NIAGARA_EMITTER_HANDLES_API
		System->GetEmitterHandles().Num();
#else
		System->GetNumEmitters();
#endif
	for (int32 i = 0; i < NumEm; ++i)
	{
		if (System->GetEmitterHandle(i).GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			OutIdx = i;
			return &System->GetEmitterHandle(i);
		}
	}
	return nullptr;
}

static UEdGraphPin* FindFirstPin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
{
	if (!Node) return nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Dir) return Pin;
	}
	return nullptr;
}

static void EnsureOutputChain(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, const FGuid& UsageId)
{
	if (!Graph) return;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UNiagaraNodeOutput* Existing = Cast<UNiagaraNodeOutput>(Node);
		if (Existing && Existing->GetUsage() == Usage) return;
	}

	UNiagaraNodeOutput* Output = NewObject<UNiagaraNodeOutput>(Graph);
	Output->CreateNewGuid();
	Output->SetUsage(Usage);
	Output->SetUsageId(UsageId);
	Output->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
	Graph->AddNode(Output, false, false);
	Output->AllocateDefaultPins();

	UNiagaraNodeInput* Input = NewObject<UNiagaraNodeInput>(Graph);
	Input->CreateNewGuid();
	Input->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
	Input->Usage = ENiagaraInputNodeUsage::Parameter;
	Graph->AddNode(Input, false, false);
	Input->AllocateDefaultPins();

	UEdGraphPin* Dst = FindFirstPin(Output, EGPD_Input);
	UEdGraphPin* Src = FindFirstPin(Input, EGPD_Output);
	if (Src && Dst) Src->MakeLinkTo(Dst);
}

static void BindScriptSource(UNiagaraScript* Script, UNiagaraScriptSource* Source)
{
	if (!Script || !Source) return;
#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
	Script->SetLatestSource(Source);
#else
	Script->SetSource(Source);
#endif
}

UNiagaraEmitter* FNexusNiagaraGraphUtils::CreateEmptyEmitter(UObject* Outer, FName Name, FString& OutError)
{
	if (!Outer)
	{
		OutError = TEXT("CreateEmptyEmitter requires Outer");
		return nullptr;
	}
	UNiagaraEmitter* NewEmitter = NewObject<UNiagaraEmitter>(Outer, Name, RF_Transactional);
	if (!NewEmitter)
	{
		OutError = TEXT("Failed to create empty NiagaraEmitter");
		return nullptr;
	}

	UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(NewEmitter, NAME_None, RF_Transactional);
	UNiagaraGraph* CreatedGraph = NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional);
	Source->NodeGraph = CreatedGraph;

#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
	NewEmitter->CheckVersionDataAvailable();
	FVersionedNiagaraEmitterData* Data = NewEmitter->GetLatestEmitterData();
	if (!Data)
	{
		OutError = TEXT("GetLatestEmitterData failed");
		return nullptr;
	}
	Data->SimTarget = ENiagaraSimTarget::CPUSim;
	Data->GraphSource = Source;
	BindScriptSource(Data->SpawnScriptProps.Script, Source);
	BindScriptSource(Data->UpdateScriptProps.Script, Source);
	BindScriptSource(Data->EmitterSpawnScriptProps.Script, Source);
	BindScriptSource(Data->EmitterUpdateScriptProps.Script, Source);
	BindScriptSource(Data->GetGPUComputeScript(), Source);
	if (!Data->SpawnScriptProps.Script || !Data->UpdateScriptProps.Script
		|| !Data->EmitterSpawnScriptProps.Script || !Data->EmitterUpdateScriptProps.Script)
	{
		OutError = TEXT("Empty emitter script not initialized");
		return nullptr;
	}
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::EmitterSpawnScript, Data->EmitterSpawnScriptProps.Script->GetUsageId());
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::EmitterUpdateScript, Data->EmitterUpdateScriptProps.Script->GetUsageId());
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::ParticleSpawnScript, Data->SpawnScriptProps.Script->GetUsageId());
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::ParticleUpdateScript, Data->UpdateScriptProps.Script->GetUsageId());
#else
	NewEmitter->SimTarget = ENiagaraSimTarget::CPUSim;
	NewEmitter->GraphSource = Source;
	BindScriptSource(NewEmitter->SpawnScriptProps.Script, Source);
	BindScriptSource(NewEmitter->UpdateScriptProps.Script, Source);
	BindScriptSource(NewEmitter->EmitterSpawnScriptProps.Script, Source);
	BindScriptSource(NewEmitter->EmitterUpdateScriptProps.Script, Source);
	BindScriptSource(NewEmitter->GetGPUComputeScript(), Source);
	if (!NewEmitter->SpawnScriptProps.Script || !NewEmitter->UpdateScriptProps.Script
		|| !NewEmitter->EmitterSpawnScriptProps.Script || !NewEmitter->EmitterUpdateScriptProps.Script)
	{
		OutError = TEXT("Empty emitter script not initialized");
		return nullptr;
	}
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::EmitterSpawnScript, NewEmitter->EmitterSpawnScriptProps.Script->GetUsageId());
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::EmitterUpdateScript, NewEmitter->EmitterUpdateScriptProps.Script->GetUsageId());
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::ParticleSpawnScript, NewEmitter->SpawnScriptProps.Script->GetUsageId());
	EnsureOutputChain(CreatedGraph, ENiagaraScriptUsage::ParticleUpdateScript, NewEmitter->UpdateScriptProps.Script->GetUsageId());
#endif
	return NewEmitter;
}

UNiagaraScriptSource* FNexusNiagaraGraphUtils::GetScriptSource(const FNiagaraEmitterHandle& Handle)
{
#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
	FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
	return Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
#else
	UNiagaraEmitter* Emitter = Handle.GetInstance();
	return Emitter ? Cast<UNiagaraScriptSource>(Emitter->GraphSource) : nullptr;
#endif
}

ENiagaraScriptUsage FNexusNiagaraGraphUtils::ParseUsage(const FString& Usage)
{
	if (Usage.Equals(TEXT("Spawn"), ESearchCase::IgnoreCase)
		|| Usage.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase)
		|| Usage.Equals(TEXT("ParticleSpawnScript"), ESearchCase::IgnoreCase))
	{
		return ENiagaraScriptUsage::ParticleSpawnScript;
	}
	if (Usage.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase)
		|| Usage.Equals(TEXT("EmitterSpawnScript"), ESearchCase::IgnoreCase))
	{
		return ENiagaraScriptUsage::EmitterSpawnScript;
	}
	if (Usage.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase)
		|| Usage.Equals(TEXT("EmitterUpdateScript"), ESearchCase::IgnoreCase))
	{
		return ENiagaraScriptUsage::EmitterUpdateScript;
	}
	return ENiagaraScriptUsage::ParticleUpdateScript;
}

FString FNexusNiagaraGraphUtils::UsageToString(ENiagaraScriptUsage Usage)
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript: return TEXT("Spawn");
	case ENiagaraScriptUsage::ParticleUpdateScript: return TEXT("Update");
	case ENiagaraScriptUsage::EmitterSpawnScript: return TEXT("EmitterSpawn");
	case ENiagaraScriptUsage::EmitterUpdateScript: return TEXT("EmitterUpdate");
	default: return TEXT("Update");
	}
}

bool FNexusNiagaraGraphUtils::AddModule(UNiagaraSystem* System, const FString& EmitterName,
	const FString& ModulePath, const FString& Usage, FString& OutModuleName, FString& OutError)
{
	int32 Idx = INDEX_NONE;
	FNiagaraEmitterHandle* Handle = FindHandleByName(System, EmitterName, Idx);
	if (!Handle)
	{
		OutError = FString::Printf(TEXT("Emitter not found: %s"), *EmitterName);
		return false;
	}
	UNiagaraScriptSource* Source = GetScriptSource(*Handle);
	if (!Source || !Source->NodeGraph)
	{
		OutError = TEXT("Emitter has no module graph (GraphSource)");
		return false;
	}
	const ENiagaraScriptUsage ScriptUsage = ParseUsage(Usage);
	EnsureOutputChain(Source->NodeGraph, ScriptUsage, FGuid());

	TArray<TSharedPtr<FJsonObject>> Before;
	CollectModules(*Handle, Before);
	TSet<FString> BeforeNames;
	for (const TSharedPtr<FJsonObject>& M : Before)
	{
		FString N;
		if (M.IsValid() && M->TryGetStringField(TEXT("name"), N)) BeforeNames.Add(N);
	}

	bool bFoundModule = false;
	const bool bAdded = Source->AddModuleIfMissing(ModulePath, ScriptUsage, bFoundModule);
	if (!bFoundModule)
	{
		OutError = FString::Printf(TEXT("NiagaraScript not found: %s"), *ModulePath);
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> After;
	CollectModules(*Handle, After);
	for (const TSharedPtr<FJsonObject>& M : After)
	{
		FString N;
		if (!M.IsValid() || !M->TryGetStringField(TEXT("name"), N) || N.IsEmpty()) continue;
		if (!BeforeNames.Contains(N))
		{
			OutModuleName = N;
			break;
		}
	}
	if (OutModuleName.IsEmpty() && After.Num() > 0)
	{
		After.Last()->TryGetStringField(TEXT("name"), OutModuleName);
	}
	if (!bAdded && OutModuleName.IsEmpty())
	{
		OutError = TEXT("AddModuleIfMissing did not add module (may exist or no Output node)");
		return false;
	}
	return true;
}

bool FNexusNiagaraGraphUtils::RemoveModule(UNiagaraSystem* System, const FString& EmitterName,
	const FString& ModuleName, FString& OutError)
{
	int32 Idx = INDEX_NONE;
	FNiagaraEmitterHandle* Handle = FindHandleByName(System, EmitterName, Idx);
	if (!Handle)
	{
		OutError = FString::Printf(TEXT("Emitter not found: %s"), *EmitterName);
		return false;
	}
	UNiagaraScriptSource* Source = GetScriptSource(*Handle);
	if (!Source || !Source->NodeGraph)
	{
		OutError = TEXT("Emitter has no module graph (GraphSource)");
		return false;
	}
	UNiagaraNodeFunctionCall* Found = nullptr;
	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
		if (!Call) continue;
		const FString Fn = Call->GetFunctionName();
		if (Fn.Equals(ModuleName, ESearchCase::IgnoreCase)
			|| Call->GetName().Equals(ModuleName, ESearchCase::IgnoreCase))
		{
			Found = Call;
			break;
		}
	}
	if (!Found)
	{
		OutError = FString::Printf(TEXT("Module not found: %s"), *ModuleName);
		return false;
	}

	UEdGraphPin* InPin = FindFirstPin(Found, EGPD_Input);
	UEdGraphPin* OutPin = FindFirstPin(Found, EGPD_Output);
	UEdGraphPin* Upstream = (InPin && InPin->LinkedTo.Num() > 0) ? InPin->LinkedTo[0] : nullptr;
	TArray<UEdGraphPin*> Downstream;
	if (OutPin)
	{
		Downstream = OutPin->LinkedTo;
	}
	if (InPin) InPin->BreakAllPinLinks();
	if (OutPin) OutPin->BreakAllPinLinks();
	if (Upstream)
	{
		for (UEdGraphPin* Dst : Downstream)
		{
			if (Dst) Upstream->MakeLinkTo(Dst);
		}
	}
	Source->NodeGraph->RemoveNode(Found);
	return true;
}

void FNexusNiagaraGraphUtils::CollectModules(const FNiagaraEmitterHandle& Handle, TArray<TSharedPtr<FJsonObject>>& OutModules)
{
	UNiagaraScriptSource* Source = GetScriptSource(Handle);
	if (!Source || !Source->NodeGraph) return;
	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
		if (!Call) continue;
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		const FString Fn = Call->GetFunctionName();
		M->SetStringField(TEXT("name"), Fn.IsEmpty() ? Call->GetName() : Fn);
		OutModules.Add(M);
	}
}

#endif // WITH_NIAGARA && WITH_EDITOR
