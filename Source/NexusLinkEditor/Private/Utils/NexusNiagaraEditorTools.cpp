// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusNiagaraEditorTools.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusEditorControlServices.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpSchemaBuilder.h"
#include "JsonObjectConverter.h"

#if WITH_NIAGARA && NX_UE_HAS_NIAGARA_EXTERNAL_EDIT
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraDataInterface.h"
#include "NiagaraExternalSystemEditorUtilities.h"

static TSharedPtr<FJsonObject> NexusNiagaraExportStruct(const UStruct* Type, const void* Data);
static bool NexusNiagaraImportStruct(const TSharedPtr<FJsonObject>& Json, const UStruct* Type, void* Data, FString& Error);

static bool NexusNiagaraImportInstanced(const TSharedPtr<FJsonObject>& Json, const UStruct* WrapperType, FInstancedStruct& Instance, FString& Error)
{
    const TSharedPtr<FJsonObject>* Fields = nullptr;
    UScriptStruct* ValueType = Cast<UScriptStruct>(FNexusEditorDomainTools::Resolve(Json, TEXT("struct")));
    if (!Json.IsValid() || !ValueType || !Json->TryGetObjectField(TEXT("value"), Fields)) { Error = TEXT("Instanced values require struct.refPath and value fields"); return false; }
    TArray<FString> AdditionalTypes; WrapperType->GetMetaData(TEXT("AdditionalTypes")).ParseIntoArray(AdditionalTypes, TEXT(","), true);
    const bool bAdditionalType = AdditionalTypes.Contains(ValueType->GetPathName());
    if (WrapperType->IsChildOf(FNiagaraExt_StackInputValue::StaticStruct()) && !ValueType->IsChildOf(FNiagaraExt_StackInputData::StaticStruct()) && !bAdditionalType)
    { Error = TEXT("Stack input values require a NiagaraExt_StackInputData struct returned by inspection"); return false; }
    if (WrapperType->IsChildOf(FNiagaraExt_VariableValue::StaticStruct()))
    {
        if (!ValueType->IsChildOf(FNiagaraExt_VariableValueBase::StaticStruct()) && !bAdditionalType) { Error = TEXT("Unexpected Niagara variable value struct"); return false; }
    }
    Instance.InitializeAs(ValueType);
    return NexusNiagaraImportStruct(*Fields, ValueType, Instance.GetMutableMemory(), Error);
}

static TSharedPtr<FJsonValue> NexusNiagaraExportProperty(FProperty* Property, const void* Data)
{
    if (const auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Object = ObjectProperty->GetObjectPropertyValue(Data);
        if (!Object) return MakeShared<FJsonValueNull>();
        return MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Object->GetPathName()));
    }
    if (const auto* StructProperty = CastField<FStructProperty>(Property))
    {
        if (StructProperty->Struct == FNiagaraTypeDefinition::StaticStruct()) return MakeShared<FJsonValueObject>(NexusNiagaraExportStruct(StructProperty->Struct, Data));
        if (StructProperty->Struct->IsChildOf(FInstancedStruct::StaticStruct()))
        {
            const auto* Value = static_cast<const FInstancedStruct*>(Data);
            if (!Value->IsValid()) return MakeShared<FJsonValueNull>();
            auto Json = MakeShared<FJsonObject>();
            Json->SetObjectField(TEXT("struct"), FNexusEditorDomainTools::Reference(Value->GetScriptStruct()->GetPathName()));
            Json->SetObjectField(TEXT("value"), NexusNiagaraExportStruct(Value->GetScriptStruct(), Value->GetMemory()));
            return MakeShared<FJsonValueObject>(Json);
        }
    }
    return nullptr;
}

static TSharedPtr<FJsonObject> NexusNiagaraExportStruct(const UStruct* Type, const void* Data)
{
    auto Json = MakeShared<FJsonObject>();
    if (Type == FNiagaraTypeDefinition::StaticStruct())
    {
        const auto* Definition = static_cast<const FNiagaraTypeDefinition*>(Data);
        const UObject* Object = Definition->IsEnum() ? static_cast<const UObject*>(Definition->GetEnum()) : static_cast<const UObject*>(Definition->GetStruct());
        if (Object) Json->SetObjectField(TEXT("classStructOrEnum"), FNexusEditorDomainTools::Reference(Object->GetPathName()));
        if (Definition->GetFlags()) Json->SetNumberField(TEXT("flags"), Definition->GetFlags());
        return Json;
    }
    if (Type->IsChildOf(FInstancedStruct::StaticStruct()))
    {
        const auto* Value = static_cast<const FInstancedStruct*>(Data);
        if (!Value->IsValid()) return Json;
        Json->SetObjectField(TEXT("struct"), FNexusEditorDomainTools::Reference(Value->GetScriptStruct()->GetPathName()));
        Json->SetObjectField(TEXT("value"), NexusNiagaraExportStruct(Value->GetScriptStruct(), Value->GetMemory()));
        return Json;
    }
    const auto Callback = FJsonObjectConverter::CustomExportCallback::CreateStatic(&NexusNiagaraExportProperty);
    FJsonObjectConverter::UStructToJsonObject(Type, Data, Json, 0, CPF_Transient, &Callback);
    return Json;
}

static bool NexusNiagaraImportStruct(const TSharedPtr<FJsonObject>& Json, const UStruct* Type, void* Data, FString& Error)
{
    if (!Json.IsValid()) { Error = TEXT("Expected an object for ") + Type->GetName(); return false; }
    if (Type == FNiagaraTypeDefinition::StaticStruct())
    {
        UObject* Object = FNexusEditorDomainTools::Resolve(Json, TEXT("classStructOrEnum"));
        if (!Object || (!Cast<UClass>(Object) && !Cast<UScriptStruct>(Object) && !Cast<UEnum>(Object))) { Error = TEXT("Niagara types require a discovered class, struct or enum reference"); return false; }
        if (auto* Struct = Cast<UScriptStruct>(Object)) if (!FNiagaraTypeHelper::IsNiagaraFriendlyTopLevelStruct(Struct, ENiagaraStructConversion::UserFacing)) { Error = TEXT("The referenced struct is not a Niagara-supported value type"); return false; }
        // 使用构造器初始化底层种类及缓存，不接受客户端伪造 UnderlyingType。
        auto* Definition = static_cast<FNiagaraTypeDefinition*>(Data); *Definition = FNiagaraTypeDefinition(Object);
        const double Flags = FNexusArgs(Json).Num(TEXT("flags"));
        if (Flags != 0.0 && Flags != static_cast<double>(FNiagaraTypeDefinition::TF_Static)) { Error = TEXT("Only the Niagara static type flag may be supplied"); return false; }
        Definition->SetFlags(static_cast<FNiagaraTypeDefinition::FTypeFlags>(static_cast<uint8>(Flags))); return true;
    }
    if (Type->IsChildOf(FInstancedStruct::StaticStruct())) return NexusNiagaraImportInstanced(Json, Type, *static_cast<FInstancedStruct*>(Data), Error);
    const auto Callback = FJsonObjectConverter::CustomImportCallback::CreateLambda([&Error](const TSharedPtr<FJsonValue>& Value, FProperty* Property, void* Address)
    {
        if (auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            if (Value->IsNull()) { ObjectProperty->SetObjectPropertyValue(Address, nullptr); return true; }
            auto Wrapper = MakeShared<FJsonObject>(); Wrapper->SetField(TEXT("Object"), Value);
            UObject* Object = FNexusEditorDomainTools::Resolve(Wrapper, TEXT("Object"));
            if (!Object || !Object->IsA(ObjectProperty->PropertyClass)) { Error = TEXT("Invalid object reference for ") + Property->GetName(); return false; }
            if (const auto* ClassProperty = CastField<FClassProperty>(Property))
            {
                const UClass* Class = Cast<UClass>(Object);
                if (!Class || !Class->IsChildOf(ClassProperty->MetaClass)) { Error = TEXT("Class does not satisfy ") + Property->GetName(); return false; }
            }
            ObjectProperty->SetObjectPropertyValue(Address, Object); return true;
        }
        if (const auto* StructProperty = CastField<FStructProperty>(Property))
        {
            if (StructProperty->Struct == FNiagaraTypeDefinition::StaticStruct()) return NexusNiagaraImportStruct(Value->AsObject(), StructProperty->Struct, Address, Error);
            if (StructProperty->Struct->IsChildOf(FInstancedStruct::StaticStruct()))
            {
                auto* Instance = static_cast<FInstancedStruct*>(Address);
                if (Value->IsNull()) { Instance->Reset(); return true; }
                return NexusNiagaraImportInstanced(Value->AsObject(), StructProperty->Struct, *Instance, Error);
            }
        }
        return false;
    });
    FText Failure;
    const bool bConverted = FJsonObjectConverter::JsonObjectToUStruct(Json.ToSharedRef(), Type, Data, 0, CPF_Transient, false, &Failure, &Callback);
    if (!bConverted && Error.IsEmpty()) Error = Failure.ToString();
    return bConverted && Error.IsEmpty();
}

template<typename T>
static bool NexusNiagaraRead(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, T& Value, FString& Error)
{
    const TSharedPtr<FJsonObject>* Json = nullptr;
    if (!Args->TryGetObjectField(Key, Json)) { Error = FString(TEXT("Missing object: ")) + Key; return false; }
    return NexusNiagaraImportStruct(*Json, T::StaticStruct(), &Value, Error);
}

template<typename T>
static void NexusNiagaraWrite(const T& Value, TSharedPtr<FJsonObject>& Result)
{
    Result = NexusNiagaraExportStruct(T::StaticStruct(), &Value);
}

static bool NexusNiagaraContextResult(const FNiagaraExternalEditContext& Context, FString& Error)
{
    TArray<FString> Errors;
    for (const FText& Message : Context.Errors) Errors.Add(Message.ToString());
    Error = FString::Join(Errors, TEXT("\n"));
    return Errors.Num() == 0;
}

static FNiagaraExt_StackItemReference NexusNiagaraStackReference(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, FString& Error)
{
    FNiagaraExt_StackItemReference Reference;
    NexusNiagaraRead(Args, Key, Reference, Error);
    return Reference;
}
#endif

bool FNexusNiagaraEditorTools::Execute(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
{
#if WITH_NIAGARA && NX_UE_HAS_NIAGARA_EXTERNAL_EDIT
    const FNexusArgs Arguments(Args);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(FNexusEditorDomainTools::Resolve(Args, TEXT("system")));
    FNiagaraExternalEditContext Context(System);
    if (Tool == TEXT("GetSystemSchema")) { FNiagaraExt_SystemSchema Value; UNiagaraExternalEditUtilities::GetSystemSchema(Value); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("GetEmitterSchema")) { FNiagaraExt_EmitterSchema Value; UNiagaraExternalEditUtilities::GetEmitterSchema(Value); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("GetRendererSchema"))
    {
        UClass* Class = Cast<UClass>(FNexusEditorDomainTools::Resolve(Args, TEXT("rendererClass")));
        if (!Class || !Class->IsChildOf(UNiagaraRendererProperties::StaticClass())) { Error = TEXT("RendererClass must derive from NiagaraRendererProperties"); return false; }
        FNiagaraExt_RendererSchema Value; UNiagaraExternalEditUtilities::GetRendererSchema(Class, Value); NexusNiagaraWrite(Value, Result);
    }
    else if (Tool == TEXT("GetDataInterfaceSchema"))
    {
        UClass* Class = Cast<UClass>(FNexusEditorDomainTools::Resolve(Args, TEXT("dataInterfaceClass")));
        if (!Class || !Class->IsChildOf(UNiagaraDataInterface::StaticClass())) { Error = TEXT("DataInterfaceClass must derive from NiagaraDataInterface"); return false; }
        FNiagaraExt_DataInterfaceSchema Value; UNiagaraExternalEditUtilities::GetDataInterfaceSchema(Class, Value); NexusNiagaraWrite(Value, Result);
    }
    else if (Tool == TEXT("GetAvailableDynamicInputs"))
    {
        FNiagaraTypeDefinition Type;
        if (!NexusNiagaraRead(Args, TEXT("type"), Type, Error)) return false;
        TArray<UNiagaraScript*> Scripts; UNiagaraExternalEditUtilities::GetAvailableDynamicInputs(Type, Scripts, Context);
        TArray<TSharedPtr<FJsonValue>> Items; for (UNiagaraScript* Script : Scripts) if (Script) Items.Add(MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Script->GetPathName())));
        Result->SetArrayField(TEXT("Items"), Items);
    }
    else if (Tool == TEXT("GetSystemSummary")) { FNiagaraExt_SystemSummary Value; UNiagaraExternalEditUtilities::GetSystemSummary(System, Value, Context); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("GetSystemDependencies")) { FNiagaraExt_SystemDependencies Value; UNiagaraExternalEditUtilities::GetSystemDependencies(System, Value, Context); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("GetSystemCompileState")) { FNiagaraExt_SystemCompileState Value; UNiagaraExternalEditUtilities::GetSystemCompileState(System, Value, Context); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("GetStackIssues")) { FNiagaraExt_StackIssues Value; UNiagaraExternalEditUtilities::GetStackIssues(System, Value, Context); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("ApplyStackIssueFix"))
    {
        FNiagaraExt_ApplyStackIssueFixResult Value;
        UNiagaraExternalEditUtilities::ApplyStackIssueFix(System, Arguments.Str(TEXT("issueId")), Arguments.Str(TEXT("fixId")), Value, Context); NexusNiagaraWrite(Value, Result);
    }
    else if (Tool == TEXT("GetSystemData")) { FNiagaraExt_SystemData Value; UNiagaraExternalEditUtilities::GetSystemData(System, Value, Context); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("GetUserVariables")) { FNiagaraExt_UserVariables Value; UNiagaraExternalEditUtilities::GetUserVariables(System, Value, Context); NexusNiagaraWrite(Value, Result); }
    else if (Tool == TEXT("SetSystemData"))
    {
        FNiagaraExt_SystemData Value; if (!NexusNiagaraRead(Args, TEXT("systemData"), Value, Error)) return false;
        UNiagaraExternalEditUtilities::SetSystemData(System, Value, Context);
    }
    else if (Tool == TEXT("AddUserVariables") || Tool == TEXT("RemoveUserVariables"))
    {
        const bool bAdd = Tool == TEXT("AddUserVariables");
        const TArray<TSharedPtr<FJsonValue>>* JsonVariables = nullptr;
        if (!System || !Args->TryGetArrayField(bAdd ? TEXT("variablesToAdd") : TEXT("variablesToRemove"), JsonVariables) || JsonVariables->Num() < 1 || JsonVariables->Num() > 64) { Error = TEXT("A system and 1-64 variables are required"); return false; }
        TArray<FNiagaraExt_UserVariable> Additions; TArray<FNiagaraExt_Variable> Removals; TSet<FName> Names;
        for (const auto& Json : *JsonVariables)
        {
            if (bAdd)
            {
                FNiagaraExt_UserVariable Value;
                if (!NexusNiagaraImportStruct(Json->AsObject(), Value.StaticStruct(), &Value, Error)) return false;
                if (!Value.Type.IsValid() || !Value.Name.ToString().StartsWith(TEXT("User.")) || Names.Contains(Value.Name)) { Error = TEXT("Variables require valid types and unique User.* names"); return false; }
                Names.Add(Value.Name);
                if (!Value.DefaultValue.IsValid())
                {
                    const int32 Size = Value.Type.GetSize();
                    if (Size < 1 || Size > 4096) { Error = TEXT("This variable type requires an explicit default value"); return false; }
                    TArray<uint8> Zero; Zero.SetNumZeroed(Size); FNiagaraVariant Variant; Variant.SetBytes(Zero.GetData(), Size); Value.DefaultValue.Set(Value.Type, Variant);
                }
                Additions.Add(MoveTemp(Value));
            }
            else
            {
                FNiagaraExt_Variable Value; if (!NexusNiagaraImportStruct(Json->AsObject(), Value.StaticStruct(), &Value, Error)) return false;
                if (!Value.Type.IsValid() || !Value.Name.ToString().StartsWith(TEXT("User.")) || Names.Contains(Value.Name)) { Error = TEXT("Variables require valid types and unique User.* names"); return false; }
                Names.Add(Value.Name); Removals.Add(Value);
            }
        }
        for (const auto& Value : Additions) { UNiagaraExternalEditUtilities::AddUserVariable(System, Value, Context); if (Context.HasErrors()) break; }
        for (const auto& Value : Removals) { UNiagaraExternalEditUtilities::RemoveUserVariable(System, Value, Context); if (Context.HasErrors()) break; }
    }
    else
    {
        const TCHAR* ReferenceKey = nullptr;
        if (Tool == TEXT("GetEmitterData") || Tool == TEXT("GetEmitterTopology")) ReferenceKey = TEXT("emitterRef");
        else if (Tool == TEXT("GetScriptStackTopology")) ReferenceKey = TEXT("scriptRef");
        else if (Tool == TEXT("GetModuleTopology") || Tool == TEXT("GetModuleInputValues") || Tool == TEXT("SetModuleEnabled") || Tool == TEXT("AddSetParameterEntry") || Tool == TEXT("RemoveSetParameterEntry")) ReferenceKey = TEXT("moduleRef");
        else if (Tool == TEXT("GetModuleSchema")) ReferenceKey = TEXT("moduleReference");
        else if (Tool == TEXT("GetStackInputSchema")) ReferenceKey = TEXT("inputReference");
        else if (Tool == TEXT("GetStackInputData") || Tool == TEXT("GetDynamicInputChain")) ReferenceKey = TEXT("stackInputRef");
        else if (Tool == TEXT("GetRendererData")) ReferenceKey = TEXT("rendererRef");
        else if (Tool == TEXT("SetEmitterData")) ReferenceKey = TEXT("emitter");
        else if (Tool == TEXT("SetRendererData")) ReferenceKey = TEXT("renderer");
        else if (Tool == TEXT("AddRenderer")) ReferenceKey = TEXT("newRendererLocation");
        else if (Tool == TEXT("RemoveRenderer")) ReferenceKey = TEXT("rendererToRemove");
        else if (Tool == TEXT("SetStackInputData")) ReferenceKey = TEXT("stackInputRef");
        else if (Tool == TEXT("AddSetParametersModule")) ReferenceKey = TEXT("moduleLocationRef");
        if (!ReferenceKey) { Error = TEXT("Unknown Niagara action"); return false; }
        auto Reference = NexusNiagaraStackReference(Args, ReferenceKey, Error); if (!Error.IsEmpty()) return false;
        Context = FNiagaraExternalEditContext(Reference);
        if (Tool == TEXT("GetEmitterTopology")) { FNiagaraExt_EmitterTopology Value; UNiagaraExternalEditUtilities::GetEmitterTopology(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetScriptStackTopology")) { FNiagaraExt_ScriptStackTopology Value; UNiagaraExternalEditUtilities::GetScriptStackTopology(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetModuleTopology")) { FNiagaraExt_ModuleTopology Value; UNiagaraExternalEditUtilities::GetModuleTopology(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetModuleSchema")) { FNiagaraExt_ModuleSchema Value; UNiagaraExternalEditUtilities::GetModuleSchema(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetModuleInputValues")) { FNiagaraExt_ModuleInputValues Value; UNiagaraExternalEditUtilities::GetModuleInputValues(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetStackInputSchema")) { FNiagaraExt_StackInputSchema Value; UNiagaraExternalEditUtilities::GetStackInputSchema(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetStackInputData")) { FNiagaraExt_StackInputValue Value; UNiagaraExternalEditUtilities::GetStackInputData(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetDynamicInputChain")) { FNiagaraExt_DynamicInputChainRef Value; UNiagaraExternalEditUtilities::GetDynamicInputChain(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetEmitterData")) { FNiagaraExt_EmitterData Value; UNiagaraExternalEditUtilities::GetEmitterData(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("GetRendererData")) { FNiagaraExt_RendererData Value; UNiagaraExternalEditUtilities::GetRendererData(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("SetModuleEnabled"))
        {
            if (!Args->HasTypedField<EJson::Boolean>(TEXT("bEnabled"))) { Error = TEXT("SetModuleEnabled requires bEnabled"); return false; }
            UNiagaraExternalEditUtilities::SetModuleEnabled(Reference, Arguments.Bool(TEXT("bEnabled")), Context);
        }
        else if (Tool == TEXT("RemoveRenderer")) UNiagaraExternalEditUtilities::RemoveRenderer(Reference, Context);
        else if (Tool == TEXT("RemoveSetParameterEntry")) UNiagaraExternalEditUtilities::RemoveSetParameterEntry(Reference, FName(*Arguments.Str(TEXT("parameterName"))), Context);
        else if (Tool == TEXT("AddRenderer"))
        {
            UClass* Class = Cast<UClass>(FNexusEditorDomainTools::Resolve(Args, TEXT("rendererClass")));
            if (!Class || !Class->IsChildOf(UNiagaraRendererProperties::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("RendererClass must be concrete NiagaraRendererProperties"); return false; }
            FNiagaraExt_RendererRef Value; UNiagaraExternalEditUtilities::AddRenderer(Reference, Class, Value, Context); NexusNiagaraWrite(Value, Result);
        }
        else if (Tool == TEXT("SetEmitterData")) { FNiagaraExt_EmitterData Value; if (!NexusNiagaraRead(Args, TEXT("emitterData"), Value, Error)) return false; UNiagaraExternalEditUtilities::SetEmitterData(Reference, Value, Context); }
        else if (Tool == TEXT("SetRendererData")) { FNiagaraExt_RendererData Value; if (!NexusNiagaraRead(Args, TEXT("rendererData"), Value, Error)) return false; UNiagaraExternalEditUtilities::SetRendererData(Reference, Value, Context); }
        else if (Tool == TEXT("SetStackInputData")) { FNiagaraExt_StackInputValue Value; if (!NexusNiagaraRead(Args, TEXT("inputData"), Value, Error)) return false; UNiagaraExternalEditUtilities::SetStackInputData(Reference, Value, Context); UNiagaraExternalEditUtilities::GetStackInputData(Reference, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("AddSetParameterEntry")) { FNiagaraExt_SetParameterEntry Entry; if (!NexusNiagaraRead(Args, TEXT("entry"), Entry, Error)) return false; FNiagaraExt_ModuleTopology Value; UNiagaraExternalEditUtilities::AddSetParameterEntry(Reference, Entry, Value, Context); NexusNiagaraWrite(Value, Result); }
        else if (Tool == TEXT("AddSetParametersModule"))
        {
            const TArray<TSharedPtr<FJsonValue>>* JsonParameters = nullptr; TArray<FNiagaraExt_SetParameterEntry> Parameters;
            if (!Args->TryGetArrayField(TEXT("parameters"), JsonParameters) || JsonParameters->Num() > 64) { Error = TEXT("Parameters must be an array with at most 64 entries"); return false; }
            for (const auto& Json : *JsonParameters) { FNiagaraExt_SetParameterEntry Entry; if (!NexusNiagaraImportStruct(Json->AsObject(), Entry.StaticStruct(), &Entry, Error)) return false; Parameters.Add(MoveTemp(Entry)); }
            FNiagaraExt_ModuleTopology Value; UNiagaraExternalEditUtilities::AddSetParametersModule(Reference, Parameters, Value, Context); NexusNiagaraWrite(Value, Result);
        }
    }
    return NexusNiagaraContextResult(Context, Error);
#else
    return FNexusEditorDomainTools::Niagara(Tool, Args, Result, Error);
#endif
}

void FNexusNiagaraEditorTools::Call(const FString& Tool, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation)
{
    if (!Operation.IsValid()) return;
    auto Input = MakeShared<FJsonObject>();
    for (const auto& Pair : Args->Values) { FString Key(*Pair.Key); if (!Key.IsEmpty()) Key[0] = FChar::ToLower(Key[0]); Input->SetField(Key, Pair.Value); }
    Operation->Backend = TEXT("Independent");
#if WITH_NIAGARA && NX_UE_HAS_NIAGARA_EXTERNAL_EDIT
    const bool bCompileQuery = Tool == TEXT("GetSystemCompileState") || Tool == TEXT("GetStackIssues") || Tool == TEXT("ApplyStackIssueFix");
    if (bCompileQuery)
    {
        UNiagaraSystem* System = Cast<UNiagaraSystem>(FNexusEditorDomainTools::Resolve(Input, TEXT("system")));
        if (!System) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Niagara system reference is required")); return; }
        const TWeakObjectPtr<UNiagaraSystem> WeakSystem(System);
        auto ApplyResult = MakeShared<FJsonObject>(); auto bApplied = MakeShared<bool>(false);
        const double Deadline = FPlatformTime::Seconds() + 120;
        FNexusEditorControlServices::AddTicker(FTickerDelegate::CreateLambda([WeakSystem, Operation, Tool, Input, Deadline, ApplyResult, bApplied](float)
        {
            if (!Operation->IsRunning()) return false;
            UNiagaraSystem* LiveSystem = WeakSystem.Get();
            if (!LiveSystem || FPlatformTime::Seconds() > Deadline) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, LiveSystem ? TEXT("Niagara compilation timed out") : TEXT("Niagara system was destroyed")); return false; }
            if (LiveSystem->HasActiveCompilations() || LiveSystem->HasOutstandingCompilationRequests(false)) return true;
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>(); FString Error;
            if (Tool == TEXT("ApplyStackIssueFix") && !*bApplied)
            {
                if (!Execute(Tool, Input, Data, Error)) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Error); return false; }
                ApplyResult->Values = Data->Values; *bApplied = true; return true;
            }
            if (!Execute(Tool == TEXT("ApplyStackIssueFix") ? TEXT("GetStackIssues") : Tool, Input, Data, Error)) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Error); return false; }
            if (Tool == TEXT("ApplyStackIssueFix")) { auto Combined = MakeShared<FJsonObject>(); Combined->SetObjectField(TEXT("applyResult"), ApplyResult); Combined->SetObjectField(TEXT("postFixIssues"), Data); Data = Combined; }
            auto Result = MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"), Data); FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result); return false;
        }), 0.05f);
        return;
    }
#endif
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>(); FString Error;
    if (!Execute(Tool, Input, Data, Error)) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Error); return; }
    auto Result = MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"), Data); FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
}

TSharedPtr<FJsonObject> FNexusNiagaraEditorTools::InputSchema(const FString& Tool)
{
#if WITH_NIAGARA && NX_UE_HAS_NIAGARA_EXTERNAL_EDIT
    auto Reference = FNexusSchema::Object().Required(TEXT("refPath"), FNexusSchema::Str(TEXT("Discovered object path"))).Build().ToSharedRef();
    auto Stack = FNexusSchema::Object().Required(TEXT("system"), Reference).Prop(TEXT("emitterName"), FNexusSchema::Str(TEXT("Discovered emitter name")))
        .Prop(TEXT("scriptName"), FNexusSchema::Str(TEXT("Discovered script name"))).Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("Discovered module name")))
        .Prop(TEXT("rendererIndex"), FNexusSchema::Int(TEXT("Discovered renderer index"), -1, -1)).Prop(TEXT("inputNameStack"), FNexusSchema::StrArr(TEXT("Outer to inner input names"))).Build().ToSharedRef();
    auto Schema = FNexusSchema::Object();
    const auto PropertyData = FNexusSchema::Object().Required(TEXT("propertyValues"), FNexusSchema::Str(TEXT("JSON-encoded editable property values"))).Build().ToSharedRef();
    if (Tool == TEXT("GetSystemSchema") || Tool == TEXT("GetEmitterSchema")) return Schema.Build();
    if (Tool == TEXT("GetRendererSchema")) return Schema.Required(TEXT("rendererClass"), Reference).Build();
    if (Tool == TEXT("GetDataInterfaceSchema")) return Schema.Required(TEXT("dataInterfaceClass"), Reference).Build();
    if (Tool == TEXT("GetAvailableDynamicInputs")) return Schema.Required(TEXT("type"), FNexusSchema::Object().Required(TEXT("classStructOrEnum"), Reference).Prop(TEXT("flags"), FNexusSchema::Int(TEXT("Niagara type flag: 0 or 1 (static)"), 0, 0, 1)).Build().ToSharedRef()).Build();
    if (Tool == TEXT("GetSystemCompileState") || Tool == TEXT("GetStackIssues") || Tool == TEXT("GetSystemDependencies") || Tool == TEXT("GetSystemSummary") || Tool == TEXT("GetSystemData") || Tool == TEXT("GetUserVariables")) return Schema.Required(TEXT("system"), Reference).Build();
    if (Tool == TEXT("GetEmitterData") || Tool == TEXT("GetEmitterTopology")) return Schema.Required(TEXT("emitterRef"), Stack).Build();
    if (Tool == TEXT("GetScriptStackTopology")) return Schema.Required(TEXT("scriptRef"), Stack).Build();
    if (Tool == TEXT("GetModuleTopology") || Tool == TEXT("GetModuleInputValues")) return Schema.Required(TEXT("moduleRef"), Stack).Build();
    if (Tool == TEXT("GetModuleSchema")) return Schema.Required(TEXT("moduleReference"), Stack).Build();
    if (Tool == TEXT("GetStackInputSchema")) return Schema.Required(TEXT("inputReference"), Stack).Build();
    if (Tool == TEXT("GetStackInputData") || Tool == TEXT("GetDynamicInputChain")) return Schema.Required(TEXT("stackInputRef"), Stack).Build();
    if (Tool == TEXT("GetRendererData")) return Schema.Required(TEXT("rendererRef"), Stack).Build();
    if (Tool == TEXT("ApplyStackIssueFix")) return Schema.Required(TEXT("system"), Reference).Required(TEXT("issueId"), FNexusSchema::Str(TEXT("Fresh issue ID"))).Required(TEXT("fixId"), FNexusSchema::Str(TEXT("Fix ID belonging to that issue"))).Build();
    if (Tool == TEXT("SetSystemData")) return Schema.Required(TEXT("system"), Reference).Required(TEXT("systemData"), PropertyData).Build();
    if (Tool == TEXT("SetEmitterData")) return Schema.Required(TEXT("emitter"), Stack).Required(TEXT("emitterData"), PropertyData).Build();
    if (Tool == TEXT("SetRendererData")) return Schema.Required(TEXT("renderer"), Stack).Required(TEXT("rendererData"), PropertyData).Build();
    if (Tool == TEXT("AddRenderer")) return Schema.Required(TEXT("newRendererLocation"), Stack).Required(TEXT("rendererClass"), Reference).Build();
    if (Tool == TEXT("RemoveRenderer")) return Schema.Required(TEXT("rendererToRemove"), Stack).Build();
    if (Tool == TEXT("SetModuleEnabled")) return Schema.Required(TEXT("moduleRef"), Stack).Required(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("Module enabled"))).Build();
    if (Tool == TEXT("SetStackInputData")) return Schema.Required(TEXT("stackInputRef"), Stack).Required(TEXT("inputData"), FNexusSchema::AnyObject(TEXT("NiagaraExt_StackInputValue; instanced fields use {struct:{refPath},value:{...}} as returned by inspection"))).Build();
    if (Tool == TEXT("AddSetParameterEntry")) return Schema.Required(TEXT("moduleRef"), Stack).Required(TEXT("entry"), FNexusSchema::AnyObject(TEXT("NiagaraExt_SetParameterEntry with parameter variable and value"))).Build();
    if (Tool == TEXT("RemoveSetParameterEntry")) return Schema.Required(TEXT("moduleRef"), Stack).Required(TEXT("parameterName"), FNexusSchema::Str(TEXT("Discovered parameter name"))).Build();
    if (Tool == TEXT("AddSetParametersModule")) return Schema.Required(TEXT("moduleLocationRef"), Stack).Required(TEXT("parameters"), FNexusSchema::ArrayOf(TEXT("At most 64 set-parameter entries"), FNexusSchema::AnyObject(TEXT("NiagaraExt_SetParameterEntry")))).Build();
    if (Tool == TEXT("AddUserVariables") || Tool == TEXT("RemoveUserVariables"))
    {
        auto Variable = FNexusSchema::Object().Required(TEXT("name"), FNexusSchema::Str(TEXT("User.* variable name"))).Required(TEXT("type"), FNexusSchema::Object().Required(TEXT("classStructOrEnum"), Reference).Prop(TEXT("flags"), FNexusSchema::Int(TEXT("Niagara type flag: 0 or 1 (static)"), 0, 0, 1)).Build().ToSharedRef())
            .Prop(TEXT("defaultValue"), FNexusSchema::AnyObject(TEXT("Instanced Niagara variable default: struct.refPath plus value"))).Prop(TEXT("description"), FNexusSchema::Str(TEXT("Variable description"))).Build().ToSharedRef();
        return Schema.Required(TEXT("system"), Reference).Required(Tool == TEXT("AddUserVariables") ? TEXT("variablesToAdd") : TEXT("variablesToRemove"), FNexusSchema::ArrayOf(TEXT("1-64 variables"), Variable)).Build();
    }
#endif
    return nullptr;
}
