// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorPCGCapability.h"
#if WITH_PCG
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_PCG
#include "PCGGraph.h"
#include "PCGNode.h"
#include "Utils/NexusAssetUtils.h"
#endif

void FNexusControlEditorPCGCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorPCG");
    Out.Description = TEXT("Spawn/execute PCG instances or edit node settings, graph parameters, instance overrides and subgraphs through the official UE 5.8 Toolset or supported engine compatibility actions. Writes are not transactionally rolled back; save graph assets explicitly.");
    auto Vector = FNexusSchema::Object().Required(TEXT("X"), FNexusSchema::Num(TEXT("X"))).Required(TEXT("Y"), FNexusSchema::Num(TEXT("Y"))).Required(TEXT("Z"), FNexusSchema::Num(TEXT("Z"))).Build();
    auto Parameter = FNexusSchema::Object()
        .Required(TEXT("Name"), FNexusSchema::Str(TEXT("Unique graph parameter name")))
        .Required(TEXT("Type"), FNexusSchema::Enum(TEXT("PCG scalar type"), {TEXT("Float"), TEXT("Double"), TEXT("Integer32"), TEXT("Integer64"), TEXT("Vector2"), TEXT("Vector"), TEXT("Vector4"), TEXT("Quaternion"), TEXT("Transform"), TEXT("String"), TEXT("Boolean"), TEXT("Rotator"), TEXT("Name")}))
        .Prop(TEXT("Description"), FNexusSchema::Str(TEXT("Parameter description")))
        .Prop(TEXT("ContainerType"), FNexusSchema::Enum(TEXT("Parameter container"), {TEXT("None"), TEXT("Array")}, TEXT("None")))
        .Prop(TEXT("DefaultValueJson"), FNexusSchema::Str(TEXT("Optional JSON-encoded default value; omit for the engine default"))).Build();
    Out.InputSchema = FNexusSchema::Object()
        .Required(TEXT("Action"), FNexusSchema::Enum(TEXT("PCG control"), {TEXT("Spawn"), TEXT("Execute"), TEXT("UpdateNode"), TEXT("SetGraphParameters"), TEXT("RemoveGraphParameters"), TEXT("SetInstanceParameters"), TEXT("ResetInstanceParameters"), TEXT("AddSubgraph")}))
        .Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Graph asset object path")))
        .Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("New instance name for Spawn")))
        .Prop(TEXT("Location"), Vector.ToSharedRef())
        .Prop(TEXT("ActorPath"), FNexusSchema::Str(TEXT("PCGVolume refPath from discovery")))
        .Prop(TEXT("NodePath"), FNexusSchema::Str(TEXT("PCGNode refPath from discovery for UpdateNode")))
        .Prop(TEXT("Parameters"), FNexusSchema::AnyObject(TEXT("Native settings/instance property names and JSON values; inspect schema first")))
        .Prop(TEXT("ParameterDefinitions"), FNexusSchema::ArrayOf(TEXT("1-64 graph parameter definitions; adding an existing name follows native type checks"), Parameter.ToSharedRef()))
        .Prop(TEXT("ParameterNames"), FNexusSchema::StrArr(TEXT("1-64 existing parameter names to remove/reset")))
        .Prop(TEXT("SubgraphPath"), FNexusSchema::Str(TEXT("Subgraph asset object path from AvailableSubgraphs or asset discovery")))
        .Prop(TEXT("NodeName"), FNexusSchema::Str(TEXT("Unique new subgraph node object name")))
        .Prop(TEXT("NodeTitle"), FNexusSchema::Str(TEXT("Optional node display title")))
        .Build();
    FNexusEditorToolSchema::ConfigureCompatible(Out, true, TEXT("PCGToolset"));
    Out.SearchAssetTypes = {TEXT("PCGGraph")};
    Out.RelatedCapabilities = {TEXT("GetEditorPCG"), TEXT("GetEditorOperation"), TEXT("SaveAsset")};
}
FCapabilityResult FNexusControlEditorPCGCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    const FNexusArgs A(Arguments);
    const FString Action = A.Str(TEXT("Action"));
    auto Input = MakeShared<FJsonObject>(); FString Tool;
    const TSharedPtr<FJsonObject>* Parameters = nullptr;
    Arguments->TryGetObjectField(TEXT("Parameters"), Parameters);
    const FString JsonParameters = Parameters ? FNexusJsonUtils::SerializeCondensed(*Parameters) : TEXT("");
    auto Invalid = [](const TCHAR* Message) { return FCapabilityResult::MakeArgInvalid(Message); };
    if (Action == TEXT("Spawn"))
    {
        if (A.Str(TEXT("AssetPath")).IsEmpty() || A.Str(TEXT("ActorName")).IsEmpty()) return Invalid(TEXT("Spawn requires AssetPath and ActorName"));
        Tool = TEXT("SpawnGraphInstance");
        Input->SetObjectField(TEXT("Graph"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
        Input->SetStringField(TEXT("Name"), A.Str(TEXT("ActorName"))); Input->SetStringField(TEXT("JsonParams"), JsonParameters);
        auto Location = MakeShared<FJsonObject>(), Rotation = MakeShared<FJsonObject>(), Scale = MakeShared<FJsonObject>(), Transform = MakeShared<FJsonObject>();
        const TSharedPtr<FJsonObject>* Position = nullptr; Arguments->TryGetObjectField(TEXT("Location"), Position); const FNexusArgs PositionArgs(Position ? *Position : nullptr);
        for (const FString& Axis : {TEXT("X"), TEXT("Y"), TEXT("Z")}) { Location->SetNumberField(Axis.ToLower(), PositionArgs.Num(*Axis)); Scale->SetNumberField(Axis.ToLower(), Axis == TEXT("Z") ? 10 : 25); }
        for (const FString& Axis : {TEXT("pitch"), TEXT("yaw"), TEXT("roll")}) Rotation->SetNumberField(Axis, 0);
        Transform->SetObjectField(TEXT("location"), Location); Transform->SetObjectField(TEXT("rotation"), Rotation); Transform->SetObjectField(TEXT("scale"), Scale); Input->SetObjectField(TEXT("Transform"), Transform);
    }
    else if (Action == TEXT("UpdateNode"))
    {
        if (A.Str(TEXT("NodePath")).IsEmpty() || !Parameters || (*Parameters)->Values.Num() == 0) return Invalid(TEXT("UpdateNode requires NodePath and nonempty Parameters"));
        Tool = TEXT("UpdateNode"); Input->SetObjectField(TEXT("Node"), FNexusNativeEditorTools::Reference(A.Str(TEXT("NodePath"))));
        Input->SetStringField(TEXT("JsonParams"), JsonParameters); Input->SetStringField(TEXT("NodeTitle"), A.Str(TEXT("NodeTitle")));
    }
    else if (Action == TEXT("Execute") || Action == TEXT("SetInstanceParameters") || Action == TEXT("ResetInstanceParameters"))
    {
        if (A.Str(TEXT("ActorPath")).IsEmpty()) return Invalid(TEXT("Instance actions require ActorPath"));
        Input->SetObjectField(TEXT("PCGVolume"), FNexusNativeEditorTools::Reference(A.Str(TEXT("ActorPath"))));
        if (Action == TEXT("Execute")) Tool = TEXT("ExecuteGraphInstance");
        else if (Action == TEXT("SetInstanceParameters")) {
            if (!Parameters || (*Parameters)->Values.Num() == 0) return Invalid(TEXT("SetInstanceParameters requires nonempty Parameters"));
            Tool = TEXT("SetGraphInstanceParams"); Input->SetStringField(TEXT("JsonParams"), JsonParameters);
        } else Tool = TEXT("ResetGraphInstanceParams");
    }
    else
    {
        if (A.Str(TEXT("AssetPath")).IsEmpty()) return Invalid(TEXT("Graph actions require AssetPath"));
        Input->SetObjectField(TEXT("Graph"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
        if (Action == TEXT("SetGraphParameters")) {
            const TArray<TSharedPtr<FJsonValue>>* Definitions = nullptr;
            if (!Arguments->TryGetArrayField(TEXT("ParameterDefinitions"), Definitions) || Definitions->Num() == 0 || Definitions->Num() > 64) return Invalid(TEXT("SetGraphParameters requires 1-64 ParameterDefinitions"));
            TSet<FString> Names; TArray<TSharedPtr<FJsonValue>> NativeDefinitions;
            for (const auto& Definition : *Definitions) {
                auto Source = Definition->AsObject(); const FNexusArgs DefinitionArgs(Source); const FString Name = DefinitionArgs.Str(TEXT("Name"));
                FText ParameterNameError; if (FName(*Name).IsNone() || !FName(*Name).IsValidObjectName(ParameterNameError) || Names.Contains(Name.ToLower())) return Invalid(TEXT("Parameter names must be valid, nonempty and unique")); Names.Add(Name.ToLower());
                const FString DefaultJson = DefinitionArgs.Str(TEXT("DefaultValueJson"));
                if (!DefaultJson.IsEmpty()) { TArray<TSharedPtr<FJsonValue>> Values; auto Reader = TJsonReaderFactory<>::Create(TEXT("[") + DefaultJson + TEXT("]")); if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() != 1) return Invalid(TEXT("DefaultValueJson must contain one valid JSON value")); }
                auto Native = MakeShared<FJsonObject>();
                Native->SetStringField(TEXT("name"), Name); Native->SetStringField(TEXT("type"), DefinitionArgs.Str(TEXT("Type")));
                Native->SetStringField(TEXT("description"), DefinitionArgs.Str(TEXT("Description"))); Native->SetStringField(TEXT("containerType"), DefinitionArgs.Str(TEXT("ContainerType"), TEXT("None")));
                Native->SetStringField(TEXT("defaultValueJson"), DefinitionArgs.Str(TEXT("DefaultValueJson"))); NativeDefinitions.Add(MakeShared<FJsonValueObject>(Native));
            }
            Tool = TEXT("SetGraphParams"); Input->SetArrayField(TEXT("Params"), NativeDefinitions);
        } else if (Action == TEXT("RemoveGraphParameters")) Tool = TEXT("RemoveGraphParams");
        else if (Action == TEXT("AddSubgraph")) {
            if (A.Str(TEXT("SubgraphPath")).IsEmpty() || A.Str(TEXT("NodeName")).IsEmpty() || A.Str(TEXT("SubgraphPath")) == A.Str(TEXT("AssetPath"))) return Invalid(TEXT("AddSubgraph requires a different SubgraphPath and unique NodeName"));
#if WITH_PCG
            UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(A.Str(TEXT("AssetPath")));
            UPCGGraph* Subgraph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(A.Str(TEXT("SubgraphPath")));
            if (!Graph || !Subgraph || Graph == Subgraph || Subgraph->Contains(Graph)) return Invalid(TEXT("Graph/subgraph is missing or would create a cycle"));
            if (StaticFindObject(UObject::StaticClass(), Graph, *A.Str(TEXT("NodeName")))) return Invalid(TEXT("NodeName already exists in the graph"));
#endif
            FText NameError; if (!FName(*A.Str(TEXT("NodeName"))).IsValidObjectName(NameError)) return Invalid(TEXT("NodeName is not a valid object name"));
            Tool = TEXT("AddSubgraphNode"); Input->SetObjectField(TEXT("SubGraphForNode"), FNexusNativeEditorTools::Reference(A.Str(TEXT("SubgraphPath"))));
            Input->SetStringField(TEXT("NodeName"), A.Str(TEXT("NodeName"))); Input->SetStringField(TEXT("JsonParams"), JsonParameters);
            Input->SetStringField(TEXT("NodeTitle"), A.Str(TEXT("NodeTitle"))); Input->SetStringField(TEXT("NodeComment"), TEXT(""));
            Input->SetNumberField(TEXT("XPositionIdx"), 0); Input->SetNumberField(TEXT("YPositionIdx"), 0);
        } else return Invalid(TEXT("Unknown PCG action"));
    }
    if (Action == TEXT("RemoveGraphParameters") || Action == TEXT("ResetInstanceParameters")) {
        const TArray<FString> Names = A.StrArr(TEXT("ParameterNames"));
        if (Names.Num() == 0 || Names.Num() > 64) return Invalid(TEXT("Remove/reset requires 1-64 ParameterNames"));
        TArray<TSharedPtr<FJsonValue>> Values; for (const FString& Name : Names) { if (Name.IsEmpty()) return Invalid(TEXT("ParameterNames cannot contain empty names")); Values.Add(MakeShared<FJsonValueString>(Name)); }
        Input->SetArrayField(TEXT("ParamNames"), Values);
    }
    auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
    FNexusNativeEditorTools::Call(TEXT("PCGToolset"), Tool, Input, Operation);
    return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorPCGCapability)
#endif
