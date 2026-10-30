// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusLegacyToolsets.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_PCG
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/NoExportTypes.h"
#endif

#if WITH_PCG
static bool NexusPCGImportBag(FInstancedPropertyBag& Bag, const TSharedPtr<FJsonObject>& Values, FString& Error)
{
    if (!Values.IsValid()) { Error = TEXT("Parameter object is required"); return false; }
    for (const auto& Pair : Values->Values) {
        const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(FName(*Pair.Key));
        if (!Desc || !Desc->CachedProperty || !FJsonObjectConverter::JsonValueToUProperty(Pair.Value, const_cast<FProperty*>(Desc->CachedProperty),
            Desc->CachedProperty->ContainerPtrToValuePtr<void>(Bag.GetMutableValue().GetMemory()), 0, 0)) {
            Error = TEXT("Unknown parameter or incompatible value: ") + FString(*Pair.Key); return false;
        }
    }
    return true;
}

static TSharedRef<FJsonObject> NexusPCGBagJson(const FInstancedPropertyBag* Bag)
{
    auto Result = MakeShared<FJsonObject>();
    if (Bag && Bag->GetPropertyBagStruct()) FJsonObjectConverter::UStructToJsonObject(Bag->GetPropertyBagStruct(), Bag->GetValue().GetMemory(), Result, 0, 0);
    return Result;
}

static bool NexusPCGSetInstance(UPCGGraphInstance* Instance, const TSharedPtr<FJsonObject>& Values, FString& Error)
{
    if (!Instance || !Instance->GetUserParametersStruct()) { Error = TEXT("PCG instance has no parameters"); return false; }
    FInstancedPropertyBag Candidate = *Instance->GetUserParametersStruct();
    if (!NexusPCGImportBag(Candidate, Values, Error)) return false;
    // 实例 setter 负责覆盖标志和传播通知；不直接写引擎的只读参数指针。
    for (const auto& Pair : Values->Values) {
        const auto* Desc = Candidate.FindPropertyDescByName(FName(*Pair.Key));
        const FProperty* Property = Desc->CachedProperty;
        if (!CastField<FNumericProperty>(Property) && !CastField<FBoolProperty>(Property) && !CastField<FStrProperty>(Property) && !CastField<FNameProperty>(Property)) {
            Error = TEXT("This engine compatibility backend currently supports scalar instance overrides: ") + FString(*Pair.Key); return false;
        }
    }
    Instance->Modify();
    for (const auto& Pair : Values->Values) {
        const FName Name(*Pair.Key); const auto* Desc = Candidate.FindPropertyDescByName(Name); const FProperty* Property = Desc->CachedProperty;
        const void* Data = Property->ContainerPtrToValuePtr<void>(Candidate.GetValue().GetMemory());
        if (const auto* Number = CastField<FNumericProperty>(Property)) {
            if (Number->IsFloatingPoint()) {
                if (CastField<FFloatProperty>(Property)) Instance->SetGraphParameter<float>(Name, static_cast<float>(Number->GetFloatingPointPropertyValue(Data)));
                else Instance->SetGraphParameter<double>(Name, Number->GetFloatingPointPropertyValue(Data));
            } else if (Property->GetSize() == sizeof(int32)) Instance->SetGraphParameter<int32>(Name, static_cast<int32>(Number->GetSignedIntPropertyValue(Data)));
            else Instance->SetGraphParameter<int64>(Name, Number->GetSignedIntPropertyValue(Data));
        } else if (const auto* Bool = CastField<FBoolProperty>(Property)) Instance->SetGraphParameter<bool>(Name, Bool->GetPropertyValue(Data));
        else if (const auto* String = CastField<FStrProperty>(Property)) Instance->SetGraphParameter<FString>(Name, String->GetPropertyValue(Data));
        else if (const auto* NameProperty = CastField<FNameProperty>(Property)) Instance->SetGraphParameter<FName>(Name, NameProperty->GetPropertyValue(Data));
    }
    Instance->MarkPackageDirty(); return true;
}
#endif

bool FNexusLegacyToolsets::PCG(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
{
#if WITH_PCG
    const FNexusArgs A(Args);
    UPCGGraph* Graph = Cast<UPCGGraph>(Resolve(Args, TEXT("graph")));
    APCGVolume* Volume = Cast<APCGVolume>(Resolve(Args, TEXT("pCGVolume")));
    if (!Volume) Volume = Cast<APCGVolume>(Resolve(Args, TEXT("pcgVolume")));
    UPCGComponent* Component = Volume ? Volume->FindComponentByClass<UPCGComponent>() : nullptr;
    UPCGGraphInstance* Instance = Component ? Component->GetGraphInstance() : nullptr;
    TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
    const FString Json = A.Str(TEXT("jsonParams"));
    if (!Json.IsEmpty() && !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Values)) { Error = TEXT("JsonParams must be an object"); return false; }
    if (Tool == TEXT("ListGraphInstances")) {
        TArray<TSharedPtr<FJsonValue>> Items;
        if (GEditor && GEditor->GetEditorWorldContext().World()) for (TActorIterator<APCGVolume> It(GEditor->GetEditorWorldContext().World()); It; ++It) {
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Path"), It->GetPathName()); Item->SetStringField(TEXT("Name"), It->GetName()); Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("Items"), Items); return true;
    }
    if (Tool == TEXT("GetGraphInstanceParams")) { if (!Instance) { Error = TEXT("PCG instance not found"); return false; } Result = NexusPCGBagJson(Instance->GetUserParametersStruct()); return true; }
    if (Tool == TEXT("SetGraphInstanceParams")) return NexusPCGSetInstance(Instance, Values, Error);
    if (Tool == TEXT("ResetGraphInstanceParams")) {
        if (!Instance || !Instance->GetUserParametersStruct()) { Error = TEXT("PCG instance not found"); return false; }
        const auto Names = A.StrArr(TEXT("paramNames"));
        for (const auto& Name : Names) if (!Instance->GetUserParametersStruct()->FindPropertyDescByName(FName(*Name))) { Error = TEXT("Unknown parameter: ") + Name; return false; }
        Instance->Modify(); for (const auto& Name : Names) Instance->ResetPropertyToDefault(Instance->GetUserParametersStruct()->FindPropertyDescByName(FName(*Name))->CachedProperty);
        Instance->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("UpdateNode")) {
        UPCGNode* Node = Cast<UPCGNode>(Resolve(Args, TEXT("node")));
        if (!Node || !Node->GetSettings()) { Error = TEXT("PCG node/settings not found"); return false; }
        if (!SetProperties(Node->GetSettings(), Values, Error)) return false;
        if (!A.Str(TEXT("nodeTitle")).IsEmpty()) { Node->Modify(); Node->NodeTitle = FName(*A.Str(TEXT("nodeTitle"))); }
        Node->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("GetNodeInfo")) {
        UPCGNode* Node = Cast<UPCGNode>(Resolve(Args, TEXT("node")));
        if (!Node || !Node->GetSettings()) { Error = TEXT("PCG node/settings not found"); return false; }
        Result->SetStringField(TEXT("Path"), Node->GetPathName()); Result->SetStringField(TEXT("ClassPath"), Node->GetSettings()->GetClass()->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Properties; FNexusPropertyUtils::CollectEditableProperties(Node->GetSettings(), Properties); Result->SetArrayField(TEXT("Properties"), Properties); return true;
    }
    if (!Graph) { Error = TEXT("Graph reference is required"); return false; }
    if (Tool == TEXT("GetGraphSchema")) {
        Result=NexusPCGBagJson(Graph->GetUserParametersStruct()); TArray<TSharedPtr<FJsonValue>> Parameters;
        if (const auto* Bag=Graph->GetUserParametersStruct()) if (Bag->IsValid()) for (const auto& Desc : Bag->GetPropertyBagStruct()->GetPropertyDescs()) {
            auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"),Desc.Name.ToString()); Item->SetStringField(TEXT("Type"),Desc.CachedProperty?Desc.CachedProperty->GetCPPType():TEXT("Unknown")); Parameters.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("Parameters"),Parameters); return true;
    }
    if (Tool == TEXT("SetGraphParams") || Tool == TEXT("RemoveGraphParams")) {
        FInstancedPropertyBag Candidate = *Graph->GetUserParametersStruct();
        if (Tool == TEXT("RemoveGraphParams")) {
            for (const auto& Name : A.StrArr(TEXT("paramNames"))) if (!Candidate.FindPropertyDescByName(FName(*Name))) { Error = TEXT("Unknown parameter: ") + Name; return false; }
            for (const auto& Name : A.StrArr(TEXT("paramNames"))) Candidate.RemovePropertyByName(FName(*Name));
        } else {
            const TArray<TSharedPtr<FJsonValue>>* Definitions = nullptr;
            if (!Args->TryGetArrayField(TEXT("params"), Definitions)) { Error = TEXT("Parameter definitions required"); return false; }
            for (const auto& Item : *Definitions) {
                const FNexusArgs D(Item->AsObject()); const FName Name(*D.Str(TEXT("name"))); const FString Type = D.Str(TEXT("type"));
                EPropertyBagPropertyType Kind = EPropertyBagPropertyType::None;
                if (Type == TEXT("Float")) Kind = EPropertyBagPropertyType::Float;
                else if (Type == TEXT("Double")) Kind = EPropertyBagPropertyType::Double;
                else if (Type == TEXT("Integer32")) Kind = EPropertyBagPropertyType::Int32;
                else if (Type == TEXT("Integer64")) Kind = EPropertyBagPropertyType::Int64;
                else if (Type == TEXT("Boolean")) Kind = EPropertyBagPropertyType::Bool;
                else if (Type == TEXT("String")) Kind = EPropertyBagPropertyType::String;
                else if (Type == TEXT("Name")) Kind = EPropertyBagPropertyType::Name;
                if (Kind == EPropertyBagPropertyType::None || D.Str(TEXT("containerType")) == TEXT("Array")) { Error = TEXT("Compatibility graph definitions currently require scalar numeric/bool/string/name types"); return false; }
                const auto* Existing = Candidate.FindPropertyDescByName(Name);
                if (Existing && Existing->ValueType != Kind) { Error = TEXT("Changing an existing parameter type is not supported"); return false; }
                if (!Existing) Candidate.AddProperty(Name, Kind);
                const FString Default = D.Str(TEXT("defaultValueJson"));
                if (!Default.IsEmpty()) {
                    TArray<TSharedPtr<FJsonValue>> Parsed;
                    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Default + TEXT("]")), Parsed) || Parsed.Num() != 1) { Error = TEXT("Invalid default JSON"); return false; }
                    auto Input = MakeShared<FJsonObject>(); Input->SetField(Name.ToString(), Parsed[0]); if (!NexusPCGImportBag(Candidate, Input, Error)) return false;
                }
            }
        }
        Graph->Modify();
#if NX_UE_HAS_PCG_PARAMETER_UPDATE_HELPER
        Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag) { Bag = Candidate; });
#else
        // Use the older editor property-change lifecycle so the graph can compare
        // descriptors and refresh its instances without calling private notifications.
        FProperty* ParametersProperty=FindFProperty<FProperty>(Graph->GetClass(),TEXT("UserParameters"));
        if (!ParametersProperty) { Error=TEXT("Graph parameter property is unavailable"); return false; }
        Graph->PreEditChange(ParametersProperty);
        *Graph->GetMutableUserParametersStruct_Unsafe() = Candidate;
        FPropertyChangedEvent Event(ParametersProperty,EPropertyChangeType::ValueSet);
        Graph->PostEditChangeProperty(Event);
#endif
        Graph->MarkPackageDirty(); return true;
    }
    Error = TEXT("This PCG action is not supported by this engine compatibility backend"); return false;
#else
    Error = TEXT("PCG compatibility requires the PCG plugin and supported engine APIs"); return false;
#endif
}
