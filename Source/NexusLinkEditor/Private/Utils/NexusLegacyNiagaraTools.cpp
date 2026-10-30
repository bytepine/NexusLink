// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusLegacyToolsets.h"
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_NIAGARA
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraParameterStore.h"
#include "NiagaraNodeFunctionCall.h"
#include "UObject/StructOnScope.h"
#endif

#if WITH_NIAGARA
static bool NexusNiagaraExportProperties(UObject* Target, TSharedPtr<FJsonObject>& Result)
{
    auto Values=MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> It(Target->GetClass());It;++It) if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient)) {
        auto Value=FJsonObjectConverter::UPropertyToJsonValue(*It,It->ContainerPtrToValuePtr<void>(Target),0,CPF_Transient);
        if (Value) Values->SetField(It->GetName(),Value);
    }
    FString Encoded; FJsonSerializer::Serialize(Values,TJsonWriterFactory<>::Create(&Encoded)); Result->SetStringField(TEXT("propertyValues"),Encoded); return true;
}
#endif

bool FNexusLegacyToolsets::Niagara(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
{
#if WITH_NIAGARA
    const FNexusArgs A(Args);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(Resolve(Args, TEXT("system")));
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    for (const TCHAR* Key : {TEXT("emitter"), TEXT("renderer"), TEXT("newRendererLocation"), TEXT("emitterRef"), TEXT("rendererRef"), TEXT("rendererToRemove")})
        if (Args->TryGetObjectField(Key, Ref)) break;
    if (Ref) System = Cast<UNiagaraSystem>(Resolve(*Ref, TEXT("system")));
    if (!System) { Error = TEXT("Niagara system reference is required"); return false; }
    if (Tool == TEXT("AddUserVariables") || Tool == TEXT("RemoveUserVariables")) {
        const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
        if (!Args->TryGetArrayField(Tool == TEXT("AddUserVariables") ? TEXT("variablesToAdd") : TEXT("variablesToRemove"), Variables) || Variables->Num() < 1 || Variables->Num() > 64) { Error = TEXT("Expected 1-64 variables"); return false; }
        TArray<FNiagaraVariable> Prepared; TSet<FName> Names;
        for (const auto& Item : *Variables) {
            const auto Definition = Item->AsObject(); const FNexusArgs D(Definition); const FString Name = D.Str(TEXT("name"));
            if (!Definition) { Error=TEXT("Each variable must be an object"); return false; }
            const TSharedPtr<FJsonObject>* Type = nullptr;
            UScriptStruct* Struct = Definition->TryGetObjectField(TEXT("type"), Type) ? Cast<UScriptStruct>(Resolve(*Type, TEXT("classStructOrEnum"))) : nullptr;
            const TSet<FString> SafeTypes={TEXT("/Script/Niagara.NiagaraFloat"),TEXT("/Script/Niagara.NiagaraInt32"),TEXT("/Script/Niagara.NiagaraBool"),TEXT("/Script/Niagara.NiagaraPosition"),TEXT("/Script/CoreUObject.Vector"),TEXT("/Script/CoreUObject.Vector2D"),TEXT("/Script/CoreUObject.Vector4"),TEXT("/Script/CoreUObject.LinearColor"),TEXT("/Script/CoreUObject.Quat")};
            if (!Name.StartsWith(TEXT("User.")) || Names.Contains(FName(*Name)) || !Struct || !SafeTypes.Contains(Struct->GetPathName()) || Struct->GetStructureSize() > 256) { Error = TEXT("Expected unique User.* names and a supported value struct"); return false; }
            Names.Add(FName(*Name)); FNiagaraVariable Variable{FNiagaraTypeDefinition(Struct), FName(*Name)}; Variable.AllocateData();
            if (Tool == TEXT("AddUserVariables")) {
                if (System->GetExposedParameters().IndexOf(Variable) != INDEX_NONE) { Error = TEXT("User variable already exists: ") + Name; return false; }
                const TSharedPtr<FJsonObject>* Default = nullptr; const TSharedPtr<FJsonObject>* Value = nullptr;
                if (Definition->TryGetObjectField(TEXT("defaultValue"), Default) && (*Default)->TryGetObjectField(TEXT("value"), Value)
                    && !FJsonObjectConverter::JsonObjectToUStruct((*Value).ToSharedRef(), Struct, Variable.GetData(), 0, 0)) { Error = TEXT("Invalid variable default"); return false; }
            } else if (System->GetExposedParameters().IndexOf(Variable) == INDEX_NONE) { Error = TEXT("User variable does not exist: ") + Name; return false; }
            Prepared.Add(Variable);
        }
        System->Modify();
        for (auto& Variable : Prepared) {
            if (Tool == TEXT("AddUserVariables")) { System->GetExposedParameters().AddParameter(Variable); System->GetExposedParameters().SetParameterData(Variable.GetData(), Variable); }
            else System->GetExposedParameters().RemoveParameter(Variable);
        }
        System->MarkPackageDirty(); Result->SetNumberField(TEXT("AppliedCount"), Prepared.Num()); return true;
    }
    if (Tool == TEXT("GetUserVariables")) {
        TArray<TSharedPtr<FJsonValue>> Items;
        TArray<FNiagaraVariable> Variables; System->GetExposedParameters().GetParameters(Variables);
        for (const auto& Variable : Variables) {
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"), Variable.GetName().ToString());
            UScriptStruct* Struct = Variable.GetType().GetScriptStruct();
            if (Struct) {
                auto Type = MakeShared<FJsonObject>(); Type->SetObjectField(TEXT("classStructOrEnum"), FNexusNativeEditorTools::Reference(Struct->GetPathName())); Item->SetObjectField(TEXT("type"), Type);
                auto Default = MakeShared<FJsonObject>(), Value = MakeShared<FJsonObject>();
                const uint8* Data = System->GetExposedParameters().GetParameterData(Variable);
                if (Data) FJsonObjectConverter::UStructToJsonObject(Struct, Data, Value, 0, 0);
                Default->SetObjectField(TEXT("struct"), FNexusNativeEditorTools::Reference(Struct->GetPathName())); Default->SetObjectField(TEXT("value"), Value); Item->SetObjectField(TEXT("defaultValue"), Default);
            }
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("userVariables"), Items); return true;
    }
    if (Tool == TEXT("GetSystemData")) return NexusNiagaraExportProperties(System,Result);
    if (Tool == TEXT("SetSystemData")) {
        const TSharedPtr<FJsonObject>* Data = nullptr; TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
        if (!Args->TryGetObjectField(TEXT("systemData"), Data) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FNexusArgs(*Data).Str(TEXT("propertyValues"))), Values)) { Error = TEXT("SystemData.PropertyValues must encode an object"); return false; }
        return SetProperties(System, Values, Error);
    }
    const FNiagaraEmitterHandle* Handle = nullptr;
    if (Ref) for (auto& Candidate : System->GetEmitterHandles()) if (Candidate.GetName().ToString() == FNexusArgs(*Ref).Str(TEXT("emitterName"))) { Handle = &Candidate; break; }
    if (!Handle) { Error = TEXT("Emitter reference is missing or does not belong to the system"); return false; }
#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
    UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
    auto* EmitterData = Emitter ? Emitter->GetEmitterData(Handle->GetInstance().Version) : nullptr;
    const TArray<UNiagaraRendererProperties*> Renderers = EmitterData ? EmitterData->GetRenderers() : TArray<UNiagaraRendererProperties*>();
#else
    UNiagaraEmitter* Emitter = Handle->GetInstance();
    const TArray<UNiagaraRendererProperties*> Renderers = Emitter ? Emitter->GetRenderers() : TArray<UNiagaraRendererProperties*>();
#endif
    if (!Emitter) { Error = TEXT("Emitter is unavailable"); return false; }
    const int32 Index = Ref ? static_cast<int32>(FNexusArgs(*Ref).Num(TEXT("rendererIndex"), -1)) : -1;
    if (Tool == TEXT("AddRenderer")) {
        UClass* Class = Cast<UClass>(Resolve(Args, TEXT("rendererClass")));
        if (!Class || !Class->IsChildOf(UNiagaraRendererProperties::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("RendererClass must be concrete Niagara renderer properties"); return false; }
        System->Modify(); Emitter->Modify(); UNiagaraRendererProperties* Renderer = NewObject<UNiagaraRendererProperties>(Emitter, Class, NAME_None, RF_Transactional);
#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
        Emitter->AddRenderer(Renderer, Handle->GetInstance().Version);
#else
        Emitter->AddRenderer(Renderer);
#endif
        Result->Values = (*Ref)->Values; Result->SetNumberField(TEXT("rendererIndex"), Renderers.Num()); System->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("RemoveRenderer") || Tool == TEXT("SetRendererData") || Tool == TEXT("GetRendererData")) {
        if (!Renderers.IsValidIndex(Index) || !Renderers[Index]) { Error = TEXT("Renderer index is invalid"); return false; }
        UNiagaraRendererProperties* Renderer = Renderers[Index];
        if (Tool == TEXT("GetRendererData")) return NexusNiagaraExportProperties(Renderer,Result);
        if (Tool == TEXT("SetRendererData")) {
            const TSharedPtr<FJsonObject>* Data = nullptr; TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
            if (!Args->TryGetObjectField(TEXT("rendererData"), Data) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FNexusArgs(*Data).Str(TEXT("propertyValues"))), Values)) { Error = TEXT("RendererData.PropertyValues must encode an object"); return false; }
            return SetProperties(Renderer, Values, Error);
        }
        System->Modify(); Emitter->Modify();
#if NX_UE_HAS_NIAGARA_VERSIONED_EMITTER
        Emitter->RemoveRenderer(Renderer, Handle->GetInstance().Version);
#else
        Emitter->RemoveRenderer(Renderer);
#endif
        System->MarkPackageDirty(); return true;
    }
    Error = TEXT("This deep Niagara action has no equivalent compatibility implementation on this engine"); return false;
#else
    Error = TEXT("Niagara plugin is unavailable"); return false;
#endif
}
