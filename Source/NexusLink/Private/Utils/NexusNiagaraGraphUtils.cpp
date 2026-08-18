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
#include "NiagaraParameterStore.h"
#include "NiagaraCommon.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Math/UnrealMathUtility.h"

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

static UNiagaraEmitter* EmitterFromHandle(const FNiagaraEmitterHandle& Handle)
{
	return Handle.GetInstance();
}

static FString UniqueEmitterNameOf(const FNiagaraEmitterHandle& Handle)
{
	if (UNiagaraEmitter* Em = EmitterFromHandle(Handle))
	{
		const FString Unique = Em->GetUniqueEmitterName();
		if (!Unique.IsEmpty()) return Unique;
	}
	return Handle.GetName().ToString();
}

static UNiagaraScript* ScriptForUsage(const FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage)
{
#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
	FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
	if (!Data) return nullptr;
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript: return Data->SpawnScriptProps.Script;
	case ENiagaraScriptUsage::ParticleUpdateScript: return Data->UpdateScriptProps.Script;
	case ENiagaraScriptUsage::EmitterSpawnScript: return Data->EmitterSpawnScriptProps.Script;
	case ENiagaraScriptUsage::EmitterUpdateScript: return Data->EmitterUpdateScriptProps.Script;
	default: return Data->UpdateScriptProps.Script;
	}
#else
	UNiagaraEmitter* Em = EmitterFromHandle(Handle);
	if (!Em) return nullptr;
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript: return Em->SpawnScriptProps.Script;
	case ENiagaraScriptUsage::ParticleUpdateScript: return Em->UpdateScriptProps.Script;
	case ENiagaraScriptUsage::EmitterSpawnScript: return Em->EmitterSpawnScriptProps.Script;
	case ENiagaraScriptUsage::EmitterUpdateScript: return Em->EmitterUpdateScriptProps.Script;
	default: return Em->UpdateScriptProps.Script;
	}
#endif
}

static ENiagaraScriptUsage InferModuleUsage(UNiagaraNodeFunctionCall* Call)
{
	if (!Call) return ENiagaraScriptUsage::ParticleUpdateScript;
	TSet<const UEdGraphNode*> Visited;
	TArray<UEdGraphNode*> Stack;
	Stack.Add(Call);
	while (Stack.Num() > 0 && Visited.Num() < 64)
	{
		UEdGraphNode* Node = Stack.Last();
		Stack.RemoveAt(Stack.Num() - 1);
		if (!Node || Visited.Contains(Node)) continue;
		Visited.Add(Node);
		if (UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node))
		{
			return Output->GetUsage();
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode()) Stack.Add(Linked->GetOwningNode());
			}
		}
	}
	return ENiagaraScriptUsage::ParticleUpdateScript;
}

static FNiagaraTypeDefinition TypeFromInputPin(UNiagaraNodeFunctionCall* Call, const FString& ParameterName)
{
	if (Call)
	{
		for (UEdGraphPin* Pin : Call->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			if (!Pin->PinName.ToString().Equals(ParameterName, ESearchCase::IgnoreCase)) continue;
			if (UScriptStruct* St = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get()))
			{
				return FNiagaraTypeDefinition(St);
			}
			const FString Cat = Pin->PinType.PinCategory.ToString();
			if (Cat.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) return FNiagaraTypeDefinition::GetBoolDef();
			if (Cat.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Cat.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetIntDef();
			}
			break;
		}
	}
	return FNiagaraTypeDefinition::GetFloatDef();
}

static bool IsNiagaraFloatType(const FNiagaraTypeDefinition& TypeDef, const FName TypeName)
{
	return TypeDef == FNiagaraTypeDefinition::GetFloatDef()
		|| TypeName == FName(TEXT("float")) || TypeName == FName(TEXT("Float"))
		|| TypeName == FName(TEXT("NiagaraFloat"));
}

static bool IsNiagaraIntType(const FNiagaraTypeDefinition& TypeDef, const FName TypeName)
{
	return TypeDef == FNiagaraTypeDefinition::GetIntDef()
		|| TypeName == FName(TEXT("int32")) || TypeName == FName(TEXT("Int32"))
		|| TypeName == FName(TEXT("int")) || TypeName == FName(TEXT("Integer"))
		|| TypeName == FName(TEXT("NiagaraInt32"));
}

static bool IsNiagaraBoolType(const FNiagaraTypeDefinition& TypeDef, const FName TypeName)
{
	return TypeDef == FNiagaraTypeDefinition::GetBoolDef()
		|| TypeName == FName(TEXT("bool")) || TypeName == FName(TEXT("Bool"))
		|| TypeName == FName(TEXT("NiagaraBool"));
}

static bool ParseNiagaraValueBytes(const FNiagaraTypeDefinition& TypeDef, const FString& Value,
	TArray<uint8>& OutData, FString& OutError)
{
	if (!TypeDef.IsValid() || TypeDef.GetSize() <= 0)
	{
		OutError = TEXT("Module parameter type is invalid");
		return false;
	}
	OutData.SetNumZeroed(TypeDef.GetSize());
	if (OutData.Num() <= 0 || OutData.GetData() == nullptr)
	{
		OutError = TEXT("Module parameter storage is empty");
		return false;
	}
	const FName TypeName = TypeDef.GetFName();

	if (IsNiagaraFloatType(TypeDef, TypeName))
	{
		const float FVal = FCString::Atof(*Value);
		FMemory::Memcpy(OutData.GetData(), &FVal, FMath::Min(TypeDef.GetSize(), (int32)sizeof(float)));
		return true;
	}
	if (IsNiagaraIntType(TypeDef, TypeName))
	{
		const int32 IVal = FCString::Atoi(*Value);
		FMemory::Memcpy(OutData.GetData(), &IVal, FMath::Min(TypeDef.GetSize(), (int32)sizeof(int32)));
		return true;
	}
	if (IsNiagaraBoolType(TypeDef, TypeName))
	{
		const bool BVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
		if (TypeDef.GetSize() >= (int32)sizeof(int32))
		{
			const int32 Stored = BVal ? 1 : 0;
			FMemory::Memcpy(OutData.GetData(), &Stored, sizeof(int32));
		}
		else
		{
			FMemory::Memcpy(OutData.GetData(), &BVal, sizeof(bool));
		}
		return true;
	}
	if (TypeName == FName(TEXT("Vector2D")) || TypeName == FName(TEXT("Vector2")))
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 2) Value.ParseIntoArrayWS(Parts);
		if (Parts.Num() != 2)
		{
			OutError = TEXT("Vector2 value format must be x,y");
			return false;
		}
		const FVector2D V(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
		FMemory::Memcpy(OutData.GetData(), &V, sizeof(FVector2D));
		return true;
	}
	if (TypeName == FName(TEXT("Vector")) || TypeName == FName(TEXT("Vector3")) || TypeName == FName(TEXT("Position")))
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3) Value.ParseIntoArrayWS(Parts);
		if (Parts.Num() != 3)
		{
			OutError = TEXT("Vector value format must be x,y,z");
			return false;
		}
		const FVector V(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
		FMemory::Memcpy(OutData.GetData(), &V, sizeof(FVector));
		return true;
	}
	if (UScriptStruct* Struct = TypeDef.GetScriptStruct())
	{
		Struct->InitializeStruct(OutData.GetData());
		const TCHAR* Result = Struct->ImportText(*Value, OutData.GetData(), nullptr, PPF_None, nullptr, Struct->GetName());
		if (!Result)
		{
			OutError = FString::Printf(TEXT("Unable to parse %s value: %s"), *TypeName.ToString(), *Value);
			return false;
		}
		return true;
	}
	OutError = FString::Printf(TEXT("Unsupported module parameter type: %s"), *TypeName.ToString());
	return false;
}

static FString FormatNiagaraBytes(const FNiagaraTypeDefinition& TypeDef, const uint8* Data)
{
	if (!Data) return FString();
	const FName TypeName = TypeDef.GetFName();
	if (IsNiagaraFloatType(TypeDef, TypeName))
	{
		float FVal = 0.f;
		FMemory::Memcpy(&FVal, Data, FMath::Min(TypeDef.GetSize(), (int32)sizeof(float)));
		return FString::SanitizeFloat(FVal);
	}
	if (IsNiagaraIntType(TypeDef, TypeName))
	{
		int32 IVal = 0;
		FMemory::Memcpy(&IVal, Data, FMath::Min(TypeDef.GetSize(), (int32)sizeof(int32)));
		return FString::FromInt(IVal);
	}
	if (IsNiagaraBoolType(TypeDef, TypeName))
	{
		if (TypeDef.GetSize() >= (int32)sizeof(int32))
		{
			int32 Stored = 0;
			FMemory::Memcpy(&Stored, Data, sizeof(int32));
			return Stored != 0 ? TEXT("true") : TEXT("false");
		}
		return (*Data != 0) ? TEXT("true") : TEXT("false");
	}
	return FString();
}

static FString LastNameSegment(const FString& Full)
{
	int32 Dot = INDEX_NONE;
	if (Full.FindLastChar(TCHAR('.'), Dot) && Dot >= 0 && Dot + 1 < Full.Len())
	{
		return Full.Mid(Dot + 1);
	}
	return Full;
}

bool FNexusNiagaraGraphUtils::SetModuleParameter(UNiagaraSystem* System, const FString& EmitterName,
	const FString& ModuleName, const FString& ParameterName, const FString& Usage,
	const FString& Value, FString& OutError)
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
	const FString FnName = Found->GetFunctionName();
	const FString ModName = FnName.IsEmpty() ? Found->GetName() : FnName;

	const ENiagaraScriptUsage ScriptUsage = Usage.IsEmpty()
		? InferModuleUsage(Found)
		: ParseUsage(Usage);
	UNiagaraScript* Script = ScriptForUsage(*Handle, ScriptUsage);
	if (!Script)
	{
		OutError = TEXT("Emitter script for usage not found");
		return false;
	}

	const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*ParameterName));
	const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, Found);
	FNiagaraTypeDefinition InputType = TypeFromInputPin(Found, ParameterName);
	FNiagaraVariable InputVar(InputType, Aliased.GetParameterHandleString());
	const FString UniqueName = UniqueEmitterNameOf(*Handle);
	FNiagaraVariable RapidVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
		InputVar, UniqueName.IsEmpty() ? nullptr : *UniqueName, ScriptUsage);

	FNiagaraParameterStore& Store = Script->RapidIterationParameters;
	bool bExists = Store.IndexOf(RapidVar) != INDEX_NONE;
	if (!bExists)
	{
		for (const FNiagaraVariableWithOffset& Var : Store.ReadParameterVariables())
		{
			const FString Full = Var.GetName().ToString();
			if (!Full.Contains(ModName)) continue;
			if (!LastNameSegment(Full).Equals(ParameterName, ESearchCase::IgnoreCase)) continue;
			RapidVar = FNiagaraVariable(Var.GetType(), Var.GetName());
			InputType = Var.GetType();
			bExists = true;
			break;
		}
	}

	TArray<uint8> Data;
	if (!ParseNiagaraValueBytes(InputType, Value, Data, OutError))
	{
		return false;
	}
	const int32 ExpectedSize = RapidVar.GetSizeInBytes();
	if (ExpectedSize <= 0 || Data.Num() != ExpectedSize || Data.GetData() == nullptr)
	{
		OutError = FString::Printf(TEXT("RapidIteration size mismatch for %s"), *RapidVar.GetName().ToString());
		return false;
	}

	System->Modify();
	Script->Modify();
	if (!bExists)
	{
		int32 NewOffset = INDEX_NONE;
		Store.AddParameter(RapidVar, false, false, &NewOffset);
		if (NewOffset == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Failed to add RapidIteration parameter %s"), *RapidVar.GetName().ToString());
			return false;
		}
	}
	// 禁止 SetParameterData(..., bAdd=true)：AddParameter 失败时内部 check(Offset) 会干掉编辑器
	if (!Store.SetParameterData(Data.GetData(), RapidVar, false))
	{
		OutError = FString::Printf(TEXT("Failed to set RapidIteration parameter %s"), *RapidVar.GetName().ToString());
		return false;
	}
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
		const FString ModName = Fn.IsEmpty() ? Call->GetName() : Fn;
		M->SetStringField(TEXT("name"), ModName);
		const ENiagaraScriptUsage Usage = InferModuleUsage(Call);
		M->SetStringField(TEXT("usage"), UsageToString(Usage));

		TArray<TSharedPtr<FJsonValue>> Inputs;
		if (UNiagaraScript* Script = ScriptForUsage(Handle, Usage))
		{
			for (const FNiagaraVariableWithOffset& Var : Script->RapidIterationParameters.ReadParameterVariables())
			{
				const FString Full = Var.GetName().ToString();
				if (!Full.Contains(ModName)) continue;
				TSharedPtr<FJsonObject> In = MakeShared<FJsonObject>();
				In->SetStringField(TEXT("name"), LastNameSegment(Full));
				In->SetStringField(TEXT("type"), Var.GetType().GetName());
				const uint8* Bytes = Script->RapidIterationParameters.GetParameterData(Var);
				const FString Val = FormatNiagaraBytes(Var.GetType(), Bytes);
				if (!Val.IsEmpty()) In->SetStringField(TEXT("value"), Val);
				Inputs.Add(MakeShared<FJsonValueObject>(In));
			}
		}
		M->SetArrayField(TEXT("inputs"), Inputs);
		OutModules.Add(M);
	}
}

#endif // WITH_NIAGARA && WITH_EDITOR
