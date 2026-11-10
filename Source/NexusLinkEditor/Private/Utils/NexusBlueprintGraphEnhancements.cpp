// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusBlueprintGraphEnhancements.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "EdGraphUtilities.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPinTypeUtils.h"
#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintGraph.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "UObject/UObjectIterator.h"


void FNexusBlueprintGraphEnhancements::Read(UBlueprint* Blueprint, const FString& Section, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
{
    const FNexusArgs A(Args); UEdGraph* Graph = FNexusBlueprintGraphUtils::FindBPGraph(Blueprint,A.Str(TEXT("GraphName")));
    if (!Graph) { Error=TEXT("GraphName must resolve to a graph in this Blueprint"); return; }
    const int32 Limit=FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"),100)),1,500);
    UEdGraphNode* Start=nullptr;
    if (Section==TEXT("Subgraph")) {
        Start=FNexusBlueprintGraphUtils::FindBPNode(Graph,A.Str(TEXT("NodeId")));
        if (!Start) { Error=TEXT("Subgraph requires a NodeId in the selected graph"); return; }
    }
    if (Section==TEXT("NodeTypes") && A.Str(TEXT("NameFilter")).Len()<2) { Error=TEXT("NodeTypes requires a narrow NameFilter"); return; }
    if (Section==TEXT("GraphDSL")) {
        TSet<UObject*> Nodes;
        TArray<TSharedPtr<FJsonValue>> Skipped;
        for (UEdGraphNode* Node : Graph->Nodes) if (Node) {
            if (Node->CanDuplicateNode() && !Node->IsA(UK2Node_FunctionEntry::StaticClass()) && !Node->IsA(UK2Node_FunctionResult::StaticClass())) Nodes.Add(Node);
            else Skipped.Add(MakeShared<FJsonValueString>(Node->NodeGuid.ToString()));
        }
        if (Nodes.Num()>500) { Error=TEXT("Graph text export is limited to 500 duplicable nodes"); return; }
        FString Code; FEdGraphUtilities::ExportNodesToText(Nodes,Code);
        Result->SetStringField(TEXT("Backend"),TEXT("Independent"));
        auto Text=MakeShared<FJsonObject>(); Text->SetStringField(TEXT("Format"),TEXT("UnrealGraphText"));
        Text->SetStringField(TEXT("Code"),Code); Text->SetNumberField(TEXT("NodeCount"),Nodes.Num());
        Text->SetArrayField(TEXT("SkippedNodeIds"),Skipped); Text->SetBoolField(TEXT("bAppendOnly"),true);
        Result->SetObjectField(Section,Text); return;
    }
    TArray<TSharedPtr<FJsonValue>> Items;
    if (Section==TEXT("Subgraph")) {
        TSet<UEdGraphNode*> Seen; TArray<UEdGraphNode*> Queue{Start}; const int32 Depth=FMath::Clamp(static_cast<int32>(A.Num(TEXT("MaxDepth"),8)),0,64);
        for (int32 Level=0,Begin=0;Begin<Queue.Num() && Level<=Depth && Items.Num()<Limit;++Level) {
            const int32 End=Queue.Num();
            for (int32 Index=Begin;Index<End && Items.Num()<Limit;++Index) { auto* Node=Queue[Index]; if (!Node || Seen.Contains(Node)) continue; Seen.Add(Node); Items.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::SerializeBPNode(Node)));
                if (Level<Depth) for (auto* Pin : Node->Pins) if (Pin) for (auto* Link : Pin->LinkedTo) if (Link && Link->GetOwningNode()->GetGraph()==Graph && !Seen.Contains(Link->GetOwningNode())) Queue.AddUnique(Link->GetOwningNode());
            }
            Begin=End;
        }
        Result->SetBoolField(TEXT("bTruncated"),Seen.Num()<Queue.Num());
    } else if (Section==TEXT("NodeTypes")) {
        for (TObjectIterator<UClass> It;It && Items.Num()<Limit;++It) if (It->IsChildOf(UK2Node::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract) && It->GetName().Contains(A.Str(TEXT("NameFilter")))) {
            auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("NodeClass"),It->GetPathName()); Item->SetStringField(TEXT("Name"),It->GetName()); Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        for (TObjectIterator<UFunction> It;It && Items.Num()<Limit;++It) if (It->HasAnyFunctionFlags(FUNC_BlueprintCallable|FUNC_BlueprintPure) && It->GetName().Contains(A.Str(TEXT("NameFilter")))) {
            auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("NodeClass"),TEXT("K2Node_CallFunction")); Item->SetStringField(TEXT("FunctionName"),It->GetName()); Item->SetStringField(TEXT("FunctionClass"),It->GetOuter()->GetPathName()); Items.Add(MakeShared<FJsonValueObject>(Item));
        }
    } else if (Section==TEXT("NodeTypePins")) {
        UFunction* Function=FNexusBlueprintGraphUtils::ResolveGraphFunction(A.Str(TEXT("FunctionName")),A.Str(TEXT("FunctionClass")));
        if (!Function) { Error=TEXT("NodeTypePins requires a callable FunctionName/FunctionClass from discovery"); return; }
        auto* Node=NewObject<UK2Node_CallFunction>(GetTransientPackage()); Node->SetFromFunction(Function); Node->AllocateDefaultPins();
        for (auto* Pin : Node->Pins) if (Pin) Items.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::SerializeBPPin(Pin)));
    } else { Error=TEXT("Unknown Blueprint graph section"); return; }
    Result->SetStringField(TEXT("Backend"),TEXT("Independent")); Result->SetArrayField(Section,Items);
}

void FNexusBlueprintGraphEnhancements::Write(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Context)
{
    UBlueprint* Blueprint=static_cast<UBlueprint*>(Context.Target); const FNexusArgs A(Op);
    if (!Blueprint) { Context.Entry->SetStringField(TEXT("Error"),TEXT("Blueprint is missing")); return; }
    const auto Fail=[&](const FString& Error){Context.Entry->SetStringField(TEXT("Error"),Error);};
    if (Context.Action==TEXT("SetVariableCategory") || Context.Action==TEXT("SetVariableEditable")) {
        const FName Name(*A.Str(TEXT("VariableName"))); const bool bExists=Blueprint->NewVariables.ContainsByPredicate([&](const FBPVariableDescription& Variable){return Variable.VarName==Name;});
        if (!bExists) { Fail(TEXT("Variable must be owned by this Blueprint")); return; }
        const bool bCategory=Context.Action==TEXT("SetVariableCategory");
        if (bCategory && !Op->HasTypedField<EJson::String>(TEXT("Category"))) { Fail(TEXT("SetVariableCategory requires Category")); return; }
        if (!bCategory && !Op->HasTypedField<EJson::Boolean>(TEXT("bEditable"))) { Fail(TEXT("SetVariableEditable requires bEditable")); return; }
        // 类别只改编辑器元数据，避免每个操作都重建骨架；编译仍由调用末尾统一执行。
        Blueprint->Modify(); if (bCategory) FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint,Name,nullptr,FText::FromString(A.Str(TEXT("Category"))),true); else FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint,Name,!A.Bool(TEXT("bEditable")));
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); return;
    }
    UEdGraph* Graph=FNexusBlueprintGraphUtils::FindBPGraph(Blueprint,A.Str(TEXT("GraphName")));
    if (!Graph) { Fail(TEXT("GraphName must resolve to a graph in this Blueprint")); return; }
    if (Context.Action==TEXT("WriteGraphDSL")) {
        const FString Code=A.Str(TEXT("Code"));
        if (Code.IsEmpty() || Code.Len()>2*1024*1024 || !Code.TrimStartAndEnd().StartsWith(TEXT("Begin Object"))) { Fail(TEXT("Code must be UnrealGraphText from GetAssetBlueprint GraphDSL (1 byte to 2 MiB); imports append duplicable nodes")); return; }
        // 在完整临时蓝图中试导入，保留图所属蓝图上下文；通过后才接触目标图。
        UBlueprint* Preview=DuplicateObject(Blueprint,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),Blueprint->GetClass(),TEXT("NexusGraphTextPreview")));
        UEdGraph* PreviewGraph=Preview?FNexusBlueprintGraphUtils::FindBPGraph(Preview,Graph->GetName()):nullptr;
        if (!PreviewGraph || !FEdGraphUtilities::CanImportNodesFromText(PreviewGraph,Code)) { Fail(TEXT("Graph text cannot be imported into this graph")); return; }
        TSet<UEdGraphNode*> PreviewNodes; FEdGraphUtilities::ImportNodesFromText(PreviewGraph,Code,PreviewNodes);
        if (!PreviewNodes.Num() || PreviewNodes.Num()>500) { Fail(TEXT("Graph text must import 1-500 compatible duplicable nodes")); return; }
        for (UEdGraphNode* Node : PreviewNodes) if (!Node || Node->IsA(UK2Node_FunctionEntry::StaticClass()) || Node->IsA(UK2Node_FunctionResult::StaticClass())) { Fail(TEXT("Graph text cannot append function entry or result boundary nodes")); return; }
        Graph->Modify(); Blueprint->Modify();
        TSet<UEdGraphNode*> Imported; FEdGraphUtilities::ImportNodesFromText(Graph,Code,Imported);
        if (Imported.Num()!=PreviewNodes.Num()) {
            for (UEdGraphNode* Node : Imported) if (Node) { Graph->GetSchema()->BreakNodeLinks(*Node); Graph->RemoveNode(Node); }
            Fail(TEXT("Graph text import differed from preview; imported nodes removed")); return;
        }
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (UEdGraphNode* Node : Imported) if (Node) { Node->CreateNewGuid(); Nodes.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::SerializeBPNode(Node))); }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Context.Entry->SetArrayField(TEXT("Nodes"),Nodes); Context.Entry->SetStringField(TEXT("Format"),TEXT("UnrealGraphText")); Context.Entry->SetStringField(TEXT("Backend"),TEXT("Independent")); return;
    }
    if (Context.Action==TEXT("ArrangeNodes")) {
        if (Graph->Nodes.Num()>500) { Fail(TEXT("ArrangeNodes is limited to 500 nodes")); return; }
        // 确定性拓扑层布局；有环的节点留在最后一列，不无限递归。
        TMap<UEdGraphNode*,int32> Columns; TSet<UEdGraphNode*> Pending; for (UEdGraphNode* Node : Graph->Nodes) if (Node) Pending.Add(Node);
        int32 Column=0;
        while (Pending.Num() && Column<Graph->Nodes.Num()) {
            TArray<UEdGraphNode*> Ready;
            for (auto* Node : Pending) { bool bWaiting=false; for (auto* Pin : Node->Pins) if (Pin && Pin->Direction==EGPD_Input) for (auto* Link : Pin->LinkedTo) if (Link && Pending.Contains(Link->GetOwningNode())) bWaiting=true; if (!bWaiting) Ready.Add(Node); }
            if (!Ready.Num()) { for (auto* Node : Pending) Columns.Add(Node,Column); break; }
            for (auto* Node : Ready) { Columns.Add(Node,Column); Pending.Remove(Node); } ++Column;
        }
        TMap<int32,int32> Rows; Graph->Modify(); for (UEdGraphNode* Node : Graph->Nodes) if (Node) { Node->Modify(); const int32 X=Columns.FindRef(Node); Node->NodePosX=X*360; Node->NodePosY=Rows.FindOrAdd(X)++*180; }
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); return;
    }
    if (Context.Action==TEXT("AddFunctionParameter") || Context.Action==TEXT("RemoveFunctionParameter")) {
        const FString Name=A.Str(TEXT("ParameterName")); FText NameError;
        if (Name.IsEmpty() || !FName(*Name).IsValidObjectName(NameError) || !Blueprint->FunctionGraphs.Contains(Graph)) { Fail(TEXT("Valid ParameterName and an owned function graph are required")); return; }
        const bool bInput=A.Bool(TEXT("bInput"),true); UK2Node_EditablePinBase* Terminal=nullptr;
        for (UEdGraphNode* Node : Graph->Nodes) if (bInput ? Node->IsA(UK2Node_FunctionEntry::StaticClass()) : Node->IsA(UK2Node_FunctionResult::StaticClass())) { Terminal=Cast<UK2Node_EditablePinBase>(Node); break; }
        const bool bAdd=Context.Action==TEXT("AddFunctionParameter"); FEdGraphPinType Type; FString Error;
        if (bAdd && !FNexusPinTypeUtils::ParsePinType(A.Str(TEXT("VariableType")),Type,Error)) { Fail(Error); return; }
        if (Terminal && (bAdd ? Terminal->FindPin(FName(*Name))!=nullptr : Terminal->FindPin(FName(*Name))==nullptr)) { Fail(TEXT("Parameter already exists or cannot be found")); return; }
        if (!Terminal && !bAdd) { Fail(TEXT("Function terminal is missing")); return; }
        Graph->Modify();
        if (!Terminal) { auto* Result=NewObject<UK2Node_FunctionResult>(Graph,NAME_None,RF_Transactional); Graph->AddNode(Result,true,false); Result->CreateNewGuid(); Result->PostPlacedNewNode(); Result->AllocateDefaultPins(); Terminal=Result; }
        Terminal->Modify();
        if (bAdd) { if (!Terminal->CreateUserDefinedPin(FName(*Name),Type,bInput?EGPD_Output:EGPD_Input,false)) { Fail(TEXT("Function cannot accept this pin type")); return; } }
        else Terminal->RemoveUserDefinedPinByName(FName(*Name));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return;
    }
    if (Context.Action==TEXT("WriteGraphPlan")) {
        const TArray<TSharedPtr<FJsonValue>>* Definitions=nullptr; const TArray<TSharedPtr<FJsonValue>>* Links=nullptr;
        if (!Op->TryGetArrayField(TEXT("GraphNodes"),Definitions) || Definitions->Num()<1 || Definitions->Num()>64 || !Op->TryGetArrayField(TEXT("GraphLinks"),Links) || Links->Num()>128) { Fail(TEXT("GraphPlan requires 1-64 GraphNodes and at most 128 GraphLinks")); return; }
        TSet<FString> Names; const TSet<FString> Allowed={TEXT("K2Node_CallFunction"),TEXT("K2Node_VariableGet"),TEXT("K2Node_VariableSet")};
        for (const auto& Value : *Definitions) { const auto Item=Value->AsObject(); const FNexusArgs D(Item); if (!Item || D.Str(TEXT("Id")).IsEmpty() || Names.Contains(D.Str(TEXT("Id"))) || !Allowed.Contains(D.Str(TEXT("NodeClass")))) { Fail(TEXT("GraphPlan node IDs must be unique; supported node classes are CallFunction/VariableGet/VariableSet")); return; } Names.Add(D.Str(TEXT("Id"))); }
        for (const auto& Value : *Links) { const FNexusArgs L(Value->AsObject()); if (!Names.Contains(L.Str(TEXT("From"))) || !Names.Contains(L.Str(TEXT("To"))) || L.Str(TEXT("FromPin")).IsEmpty() || L.Str(TEXT("ToPin")).IsEmpty()) { Fail(TEXT("GraphPlan links must reference plan nodes and explicit pins")); return; } }
        TArray<UEdGraphNode*> Created; TMap<FString,UEdGraphNode*> Nodes;
        const auto Rollback=[&](){for (auto* Node : Created) { Graph->GetSchema()->BreakNodeLinks(*Node); Graph->RemoveNode(Node); } FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);};
        for (const auto& Value : *Definitions) {
            auto Item=MakeShared<FJsonObject>(); Item->Values=Value->AsObject()->Values; Item->RemoveField(TEXT("Id")); Item->SetStringField(TEXT("GraphName"),Graph->GetName());
            FNexusActionContext Child=Context; Child.Action=TEXT("AddNode"); Child.Entry=MakeShared<FJsonObject>(); FNexusBlueprintGraphOps::HandleGraphNode(Item,Child);
            FString Error; if (Child.Entry->TryGetStringField(TEXT("Error"),Error)) { Rollback(); Fail(Error); return; }
            UEdGraphNode* Node=FNexusBlueprintGraphUtils::FindBPNode(Graph,FNexusArgs(Child.Entry).Str(TEXT("NodeId"))); if (!Node) { Rollback(); Fail(TEXT("Graph node creation produced no node")); return; }
            Created.Add(Node); Nodes.Add(FNexusArgs(Value->AsObject()).Str(TEXT("Id")),Node);
        }
        for (const auto& Value : *Links) { const FNexusArgs L(Value->AsObject()); auto* From=FNexusBlueprintGraphUtils::FindBPPin(Nodes[L.Str(TEXT("From"))],L.Str(TEXT("FromPin"))); auto* To=FNexusBlueprintGraphUtils::FindBPPin(Nodes[L.Str(TEXT("To"))],L.Str(TEXT("ToPin"))); if (!From || !To || From->Direction!=EGPD_Output || To->Direction!=EGPD_Input || !Graph->GetSchema()->TryCreateConnection(From,To)) { Rollback(); Fail(TEXT("GraphPlan pin connection was rejected; plan nodes removed")); return; } }
        TArray<TSharedPtr<FJsonValue>> Mapping; for (const auto& Pair : Nodes) { auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Id"),Pair.Key); Item->SetStringField(TEXT("NodeId"),Pair.Value->NodeGuid.ToString()); Mapping.Add(MakeShared<FJsonValueObject>(Item)); } Context.Entry->SetArrayField(TEXT("Nodes"),Mapping); return;
    }
    Fail(TEXT("Unknown enhanced Blueprint action"));
}
