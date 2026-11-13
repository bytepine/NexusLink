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
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Misc/DefaultValueHelper.h"
#include "UObject/UObjectIterator.h"

static void EditBlueprintStructPin(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Operation, FNexusActionContext& Context)
{
    const FNexusArgs Args(Operation);
    const FString NodeId = Args.Str(TEXT("NodeId"));
    const FString PinName = Args.Str(TEXT("PinName"));
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeId);
    UEdGraphPin* Pin = FNexusBlueprintGraphUtils::FindBPPin(Node, PinName);
    if (NodeId.IsEmpty() || PinName.IsEmpty() || !Schema || !Node || !Cast<UK2Node>(Node)
        || Node->GetGraph() != Graph || !Graph->Nodes.Contains(Node) || !Pin || !Node->Pins.Contains(Pin) || Pin->bOrphanedPin)
    {
        Context.Entry->SetStringField(TEXT("Error"), TEXT("SplitPin/RecombinePin requires NodeId and PinName of a live K2 node in the selected Blueprint graph"));
        return;
    }
    const bool bSplit = Context.Action == TEXT("SplitPin");
    // 合并既接受已拆分的父引脚，也接受其子引脚；始终让 K2 schema 校验整个待合并子树。
    UEdGraphPin* ParentPin = bSplit ? Pin : (Pin->SubPins.Num() ? Pin : Pin->ParentPin);
    UEdGraphPin* RecombineChild = !bSplit && ParentPin && ParentPin->SubPins.Num() ? ParentPin->SubPins[0] : nullptr;
    if ((bSplit && (Pin->bHidden || Pin->SubPins.Num() || !Schema->CanSplitStructPin(*Pin)))
        || (!bSplit && (!ParentPin || !Node->Pins.Contains(ParentPin) || !RecombineChild
            || !Node->Pins.Contains(RecombineChild) || !Schema->CanRecombineStructPin(*RecombineChild))))
    {
        Context.Entry->SetStringField(TEXT("Error"), bSplit
            ? TEXT("K2 schema rejected SplitPin: pin must be an unsplit, unconnected splittable struct")
            : TEXT("K2 schema rejected RecombinePin: pin must be split and all child pins must be unconnected"));
        return;
    }
    const FName ParentName = ParentPin->PinName;
    const EEdGraphPinDirection ParentDirection = ParentPin->Direction;
    if (ParentDirection == EGPD_Input)
    {
        // K2 原生拆组仅为这四类传递默认值，其他输入结构体会静默丢失父值或子值。
        const UScriptStruct* StructType = Cast<UScriptStruct>(ParentPin->PinType.PinSubCategoryObject.Get());
        if (StructType != TBaseStructure<FVector>::Get() && StructType != TBaseStructure<FRotator>::Get()
            && StructType != TBaseStructure<FVector2D>::Get() && StructType != TBaseStructure<FLinearColor>::Get())
        {
            Context.Entry->SetStringField(TEXT("Error"), TEXT("Unsupported: K2 struct pin edits preserve input defaults only for Vector/Rotator/Vector2D/LinearColor"));
            return;
        }
        if (bSplit)
        {
            FString DefaultError;
            bool bValidDefault = Schema->DefaultValueSimpleValidation(ParentPin->PinType, ParentPin->PinName,
                ParentPin->DefaultValue, ParentPin->DefaultObject, ParentPin->DefaultTextValue, &DefaultError);
            if (bValidDefault && !ParentPin->DefaultValue.IsEmpty())
            {
                if (StructType == TBaseStructure<FVector>::Get() || StructType == TBaseStructure<FRotator>::Get())
                {
                    // 原生 SplitPin 对分量数使用 check，并以 float 解析；预先拒绝额外分量及非有限值。
                    TArray<FString> Components;
                    ParentPin->DefaultValue.ParseIntoArray(Components, TEXT(","), false);
                    bValidDefault = Components.Num() == 3;
                    for (const FString& Component : Components)
                    {
                        float ParsedComponent = 0;
                        if (!FDefaultValueHelper::ParseFloat(Component, ParsedComponent) || !FMath::IsFinite(ParsedComponent)) bValidDefault = false;
                    }
                }
                else if (StructType == TBaseStructure<FVector2D>::Get())
                {
                    FVector2D ParsedVector(ForceInit);
                    bValidDefault = ParsedVector.InitFromString(ParentPin->DefaultValue) && FMath::IsFinite(ParsedVector.X) && FMath::IsFinite(ParsedVector.Y);
                }
                else
                {
                    FLinearColor ParsedColor(ForceInit);
                    bValidDefault = ParsedColor.InitFromString(ParentPin->DefaultValue) && FMath::IsFinite(ParsedColor.R)
                        && FMath::IsFinite(ParsedColor.G) && FMath::IsFinite(ParsedColor.B) && FMath::IsFinite(ParsedColor.A);
                }
            }
            if (!bValidDefault)
            {
                Context.Entry->SetStringField(TEXT("Error"), TEXT("SplitPin rejected invalid or non-finite struct input default: ") + DefaultError);
                return;
            }
        }
    }
    Blueprint->Modify(); Graph->Modify(); Node->Modify(); ParentPin->Modify();
    if (bSplit) Schema->SplitPin(Pin);
    else Schema->RecombinePin(RecombineChild);
    // RecombinePin 会销毁子引脚，禁止在调用后继续使用原 Pin 指针。
    UEdGraphNode* UpdatedNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeId);
    UEdGraphPin* UpdatedPin = UpdatedNode ? UpdatedNode->FindPin(ParentName, ParentDirection) : nullptr;
    if (!UpdatedNode || UpdatedNode->GetGraph() != Graph || !UpdatedPin
        || (bSplit ? UpdatedPin->SubPins.Num() == 0 || !UpdatedPin->bHidden : UpdatedPin->SubPins.Num() != 0 || UpdatedPin->bHidden))
    {
        Context.Entry->SetStringField(TEXT("Error"), TEXT("Struct pin edit did not produce the expected pin hierarchy"));
        return;
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Context.Entry->SetStringField(TEXT("GraphName"), Graph->GetName());
    Context.Entry->SetStringField(TEXT("NodeId"), UpdatedNode->NodeGuid.ToString());
    Context.Entry->SetStringField(TEXT("PinName"), ParentName.ToString());
    Context.Entry->SetObjectField(TEXT("Node"), FNexusBlueprintGraphUtils::SerializeBPNode(UpdatedNode));
}

static bool CanDuplicateBlueprintNode(const UEdGraphNode* Node)
{
    if (!Node || !Node->CanDuplicateNode()) return false;
    // 仅复制无附属资源的节点；Timeline/Composite/事件/边界等需要专用的资产结构操作。
    const UClass* NodeClass = Node->GetClass();
    return NodeClass == UK2Node_CallFunction::StaticClass() || NodeClass == UK2Node_VariableGet::StaticClass()
        || NodeClass == UK2Node_VariableSet::StaticClass() || NodeClass == UK2Node_Knot::StaticClass()
        || NodeClass == UEdGraphNode_Comment::StaticClass();
}

static bool CollectBlueprintCopyLinks(const TSet<UEdGraphNode*>& Nodes, bool bRequireInternal, TSet<FString>& Links)
{
    for (UEdGraphNode* Node : Nodes)
        for (UEdGraphPin* Pin : Node->Pins) if (Pin)
            for (UEdGraphPin* Other : Pin->LinkedTo)
            {
                if (!Other || !Other->GetOwningNode()) return false;
                if (!Nodes.Contains(Other->GetOwningNode())) { if (bRequireInternal) return false; continue; }
                if (!Other->LinkedTo.Contains(Pin)) return false;
                Links.Add(Node->NodeGuid.ToString() + TEXT(":") + Pin->PinName.ToString() + TEXT(":") + FString::FromInt(Pin->Direction)
                    + TEXT("->") + Other->GetOwningNode()->NodeGuid.ToString() + TEXT(":") + Other->PinName.ToString() + TEXT(":") + FString::FromInt(Other->Direction));
            }
    return true;
}

static bool ValidateBlueprintCopy(const TSet<UEdGraphNode*>& Imported, UEdGraph* Destination, const TMap<FGuid, UEdGraphNode*>& Sources, const TSet<FString>& ExpectedLinks)
{
    if (Imported.Num() != Sources.Num()) return false;
    TSet<FGuid> Seen;
    for (UEdGraphNode* Node : Imported)
    {
        if (!Node || Node->GetGraph() != Destination || !Destination->Nodes.Contains(Node) || !CanDuplicateBlueprintNode(Node)) return false;
        UEdGraphNode* const* Source = Sources.Find(Node->NodeGuid);
        if (!Source || !*Source || Node->GetClass() != (*Source)->GetClass() || Seen.Contains(Node->NodeGuid)) return false;
        Seen.Add(Node->NodeGuid);
    }
    TSet<FString> ActualLinks;
    if (!CollectBlueprintCopyLinks(Imported, true, ActualLinks) || ActualLinks.Num() != ExpectedLinks.Num()) return false;
    for (const FString& Link : ExpectedLinks) if (!ActualLinks.Contains(Link)) return false;
    return true;
}

static void DuplicateBlueprintNodes(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Operation, FNexusActionContext& Context)
{
    const auto Fail = [&](const FString& Error) { Context.Entry->SetStringField(TEXT("Error"), Error); };
    const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Operation->TryGetArrayField(TEXT("NodeIds"), NodeIds) || NodeIds->Num() < 1 || NodeIds->Num() > 64 || !Schema)
    { Fail(TEXT("DuplicateNodes requires 1-64 unique NodeIds in one K2 Blueprint graph")); return; }
    int32 OffsetX = 64, OffsetY = 64;
    for (const TCHAR* Field : {TEXT("OffsetX"), TEXT("OffsetY")})
    {
        if (!Operation->HasField(Field)) continue;
        double Value = 0;
        if (!Operation->TryGetNumberField(Field, Value) || !FMath::IsFinite(Value)
            || Value < MIN_int32 || Value > MAX_int32 || Value != static_cast<double>(static_cast<int32>(Value)))
        { Fail(TEXT("OffsetX/OffsetY must be int32 integers")); return; }
        if (FCString::Strcmp(Field, TEXT("OffsetX")) == 0) OffsetX = static_cast<int32>(Value);
        else OffsetY = static_cast<int32>(Value);
    }
    TMap<FGuid, UEdGraphNode*> Sources;
    TArray<FGuid> SourceOrder;
    TSet<UEdGraphNode*> SourceNodes;
    TSet<UObject*> ExportNodes;
    for (const TSharedPtr<FJsonValue>& Value : *NodeIds)
    {
        FString NodeId;
        UEdGraphNode* Node = Value.IsValid() && Value->TryGetString(NodeId) ? FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeId) : nullptr;
        if (!Node || Node->GetGraph() != Graph || !Graph->Nodes.Contains(Node) || !CanDuplicateBlueprintNode(Node) || Sources.Contains(Node->NodeGuid))
        { Fail(TEXT("NodeIds must be unique owned duplicable CallFunction/VariableGet/VariableSet/Knot/Comment nodes; resource and boundary nodes are unsupported")); return; }
        const int64 NewX = static_cast<int64>(Node->NodePosX) + OffsetX;
        const int64 NewY = static_cast<int64>(Node->NodePosY) + OffsetY;
        if (NewX < MIN_int32 || NewX > MAX_int32 || NewY < MIN_int32 || NewY > MAX_int32)
        { Fail(TEXT("DuplicateNodes position plus offset exceeds int32 range")); return; }
        Sources.Add(Node->NodeGuid, Node); SourceOrder.Add(Node->NodeGuid); SourceNodes.Add(Node); ExportNodes.Add(Node);
    }
    TSet<FString> ExpectedLinks;
    if (!CollectBlueprintCopyLinks(SourceNodes, false, ExpectedLinks)) { Fail(TEXT("Selected nodes contain invalid links")); return; }
    FString Code;
    FEdGraphUtilities::ExportNodesToText(ExportNodes, Code);
    if (Code.IsEmpty() || Code.Len() > 2 * 1024 * 1024) { Fail(TEXT("Selected nodes exceed the 2 MiB graph text limit")); return; }
    // 完整临时副本可能触发编译，不编译源资产；预演验证 GUID、类型及内部连线，原图尚未写入。
    UBlueprint* Preview = DuplicateObject(Blueprint, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), Blueprint->GetClass(), TEXT("NexusNodeCopyPreview")));
    UEdGraph* PreviewGraph = Preview ? FNexusBlueprintGraphUtils::FindBPGraph(Preview, Graph->GetPathName(Blueprint)) : nullptr;
    if (!PreviewGraph || !FEdGraphUtilities::CanImportNodesFromText(PreviewGraph, Code)) { Fail(TEXT("Selected nodes cannot be imported into this graph")); return; }
    TSet<UEdGraphNode*> PreviewNodes;
    FEdGraphUtilities::ImportNodesFromText(PreviewGraph, Code, PreviewNodes);
    if (!ValidateBlueprintCopy(PreviewNodes, PreviewGraph, Sources, ExpectedLinks)) { Fail(TEXT("DuplicateNodes preview did not preserve the selected nodes and internal links")); return; }
    Blueprint->Modify(); Graph->Modify();
    TSet<UEdGraphNode*> Imported;
    FEdGraphUtilities::ImportNodesFromText(Graph, Code, Imported);
    const auto RemoveImported = [&]() { for (UEdGraphNode* ImportedNode : Imported) if (ImportedNode) { Schema->BreakNodeLinks(*ImportedNode); Graph->RemoveNode(ImportedNode); } };
    if (!ValidateBlueprintCopy(Imported, Graph, Sources, ExpectedLinks))
    {
        RemoveImported();
        Fail(TEXT("DuplicateNodes import differed from preview; imported nodes removed")); return;
    }
    TMap<FGuid, UEdGraphNode*> Copies;
    for (UEdGraphNode* ImportedNode : Imported) Copies.Add(ImportedNode->NodeGuid, ImportedNode);
    auto NodeIdMap = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> NewNodes;
    for (const FGuid& SourceId : SourceOrder)
    {
        UEdGraphNode* Copy = Copies.FindRef(SourceId);
        UEdGraphNode* Source = Sources.FindRef(SourceId);
        if (!Copy || !Source) { RemoveImported(); Fail(TEXT("DuplicateNodes mapping became invalid; imported nodes removed")); return; }
        Copy->Modify(); Copy->CreateNewGuid();
        Copy->NodePosX = static_cast<int32>(static_cast<int64>(Source->NodePosX) + OffsetX);
        Copy->NodePosY = static_cast<int32>(static_cast<int64>(Source->NodePosY) + OffsetY);
        NodeIdMap->SetStringField(SourceId.ToString(), Copy->NodeGuid.ToString());
    }
    // GUID 全部替换后再序列化，保证连线端点也返回新 GUID。
    for (const FGuid& SourceId : SourceOrder)
    {
        UEdGraphNode* Copy = Copies.FindRef(SourceId);
        if (!Copy) { RemoveImported(); Fail(TEXT("DuplicateNodes mapping became invalid; imported nodes removed")); return; }
        NewNodes.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::SerializeBPNode(Copy)));
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); Graph->NotifyGraphChanged();
    Context.Entry->SetObjectField(TEXT("NodeIdMap"), NodeIdMap);
    Context.Entry->SetArrayField(TEXT("Nodes"), NewNodes);
    Context.Entry->SetStringField(TEXT("GraphName"), Graph->GetName());
    Context.Entry->SetNumberField(TEXT("OffsetX"), OffsetX); Context.Entry->SetNumberField(TEXT("OffsetY"), OffsetY);
}

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
    if (Context.Action==TEXT("SplitPin") || Context.Action==TEXT("RecombinePin") || Context.Action==TEXT("DuplicateNodes")) {
        if (!Graph->IsIn(Blueprint) || FBlueprintEditorUtils::FindBlueprintForGraph(Graph)!=Blueprint) { Fail(TEXT("Graph must be owned by the target Blueprint")); return; }
        if (Context.Action==TEXT("DuplicateNodes")) DuplicateBlueprintNodes(Blueprint,Graph,Op,Context);
        else EditBlueprintStructPin(Blueprint,Graph,Op,Context);
        return;
    }
    if (Context.Action==TEXT("WriteGraphDSL")) {
        const FString Code=A.Str(TEXT("Code"));
        if (Code.IsEmpty() || Code.Len()>2*1024*1024 || !Code.TrimStartAndEnd().StartsWith(TEXT("Begin Object"))) { Fail(TEXT("Code must be UnrealGraphText from GetAssetBlueprint GraphDSL (1 byte to 2 MiB); imports append duplicable nodes")); return; }
        // 在完整临时蓝图中试导入，保留所属蓝图上下文；临时副本可能编译，不编译源资产。
        UBlueprint* Preview=DuplicateObject(Blueprint,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),Blueprint->GetClass(),TEXT("NexusGraphTextPreview")));
        UEdGraph* PreviewGraph=Preview?FNexusBlueprintGraphUtils::FindBPGraph(Preview,Graph->GetPathName(Blueprint)):nullptr;
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
            auto Item=MakeShared<FJsonObject>(); Item->Values=Value->AsObject()->Values; Item->RemoveField(TEXT("Id")); Item->SetStringField(TEXT("GraphName"),Graph->GetPathName(Blueprint));
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
