// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAssetStructureUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "Misc/SecureHash.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/NamedSlotInterface.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#if WITH_PCG
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#endif

TArray<FString> FNexusAssetStructureUtils::Snapshot(UObject* Asset, const FString& NormalizeNode)
{
	TArray<FString> Rows;
	const auto Id = [&](const FString& Name) { return !NormalizeNode.IsEmpty() && Name == NormalizeNode ? FString(TEXT("$NewNode")) : Name; };
	if (UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(Asset))
	{
		if (Widget->WidgetTree) Widget->WidgetTree->ForEachWidget([&](UWidget* Item)
		{
			const UPanelWidget* Parent = Item->GetParent();
			Rows.Add(FString::Printf(TEXT("Widget|%s|%s|%s|%d"), *Item->GetName(), *Item->GetClass()->GetPathName(), Parent ? *Parent->GetName() : TEXT("$Root"), Parent ? Parent->GetChildIndex(Item) : 0));
			if (const INamedSlotInterface* Slots = Cast<INamedSlotInterface>(Item))
			{
				TArray<FName> Names; Slots->GetSlotNames(Names);
				for (const FName& Name : Names) if (const UWidget* Content = Slots->GetContentForSlot(Name)) Rows.Add(TEXT("NamedSlot|") + Item->GetName() + TEXT("|") + Name.ToString() + TEXT("|") + Content->GetName());
			}
		});
	}
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs) if (Graph) for (const UEdGraphNode* Node : Graph->Nodes) if (Node)
		{
			const FString NodeId = Id(Node->NodeGuid.ToString());
			Rows.Add(TEXT("Node|") + Graph->GetName() + TEXT("|") + NodeId + TEXT("|") + Node->GetClass()->GetPathName());
			for (const UEdGraphPin* Pin : Node->Pins) if (Pin && Pin->Direction == EGPD_Output) for (const UEdGraphPin* Other : Pin->LinkedTo) if (Other && Other->GetOwningNode())
				Rows.Add(TEXT("Edge|") + Graph->GetName() + TEXT("|") + NodeId + TEXT("|") + Pin->PinName.ToString() + TEXT("|") + Id(Other->GetOwningNode()->NodeGuid.ToString()) + TEXT("|") + Other->PinName.ToString());
		}
	}
#if WITH_PCG
	else if (UPCGGraph* Graph = Cast<UPCGGraph>(Asset))
	{
		TArray<UPCGNode*> Nodes = Graph->GetNodes(); Nodes.Add(Graph->GetInputNode()); Nodes.Add(Graph->GetOutputNode());
		for (const UPCGNode* Node : Nodes) if (Node)
		{
			Rows.Add(TEXT("Node|") + Id(Node->GetName()) + TEXT("|") + (Node->GetSettings() ? Node->GetSettings()->GetClass()->GetPathName() : TEXT("")));
			for (const UPCGPin* Pin : Node->GetOutputPins()) if (Pin) for (const UPCGEdge* Edge : Pin->Edges) if (Edge)
			{
				const UPCGPin* Other = Edge->GetOtherPin(Pin);
				if (Other && Other->Node) Rows.Add(TEXT("Edge|") + Id(Node->GetName()) + TEXT("|") + Pin->Properties.Label.ToString() + TEXT("|") + Id(Other->Node->GetName()) + TEXT("|") + Other->Properties.Label.ToString());
			}
		}
	}
#endif
	Rows.Sort(); return Rows;
}

FString FNexusAssetStructureUtils::Hash(UObject* Asset) { return FMD5::HashAnsiString(*FString::Join(Snapshot(Asset), TEXT("\n"))); }

bool FNexusAssetStructureUtils::Supports(const FString& Capability, const FString& Action)
{
	if (Capability == TEXT("ManageAssetUserWidget")) return Action == TEXT("Add") || Action == TEXT("Remove") || Action == TEXT("Move");
	if (Capability == TEXT("ManageAssetBlueprint")) return Action == TEXT("RemoveNode") || Action == TEXT("Disconnect") || Action == TEXT("DisconnectAll");
	if (Capability == TEXT("ManageAssetPCGGraph")) return Action == TEXT("AddNode") || Action == TEXT("RemoveNode") || Action == TEXT("AddEdge") || Action == TEXT("RemoveEdge");
	return false;
}

bool FNexusAssetStructureUtils::Check(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Args, FString& Error)
{
	const FNexusArgs A(Args);
	if (A.Bool(TEXT("bVerifyStructure")) && FNexusJsonUtils::ExtractOperations(Args).Num() > 128)
	{ Error = TEXT("Structural verification is limited to 128 operations"); return false; }
	if (Args->HasField(TEXT("ExpectedStructureHash")) && A.Str(TEXT("ExpectedStructureHash")) != Hash(Asset))
	{ Error = TEXT("ExpectedStructureHash mismatch: asset structure changed after preview"); return false; }
	if (A.Bool(TEXT("bVerifyStructure"))) for (const auto& Value : FNexusJsonUtils::ExtractOperations(Args))
		if (!Value || Value->Type != EJson::Object || !Supports(Capability, FNexusArgs(Value->AsObject()).Str(TEXT("Action"))))
		{ Error = TEXT("bVerifyStructure only supports the structural actions documented by GetAssetEditPreview"); return false; }
	return true;
}

UObject* FNexusAssetStructureUtils::Copy(UObject* Asset)
{
	if (!Asset) return nullptr;
	UObject* Result = nullptr;
	if (UBlueprint* Source = Cast<UBlueprint>(Asset))
	{
		// 整体 DuplicateObject 会触发 Blueprint::PostDuplicate 编译；只复制拥有的结构对象。
		UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage(), Source->GetClass(), NAME_None, RF_Transient);
		Blueprint->ParentClass = Source->ParentClass; Blueprint->BlueprintType = Source->BlueprintType;
		Blueprint->NewVariables = Source->NewVariables;
		const auto CopyGraphs = [Blueprint](const auto& Input, auto& Output)
		{
			for (UEdGraph* Graph : Input) if (Graph) Output.Add(DuplicateObject<UEdGraph>(Graph, Blueprint, Graph->GetFName()));
		};
		CopyGraphs(Source->UbergraphPages, Blueprint->UbergraphPages);
		CopyGraphs(Source->FunctionGraphs, Blueprint->FunctionGraphs);
		CopyGraphs(Source->MacroGraphs, Blueprint->MacroGraphs);
		CopyGraphs(Source->DelegateSignatureGraphs, Blueprint->DelegateSignatureGraphs);
		if (UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(Source))
		{
			UWidgetBlueprint* WidgetCopy = Cast<UWidgetBlueprint>(Blueprint);
			WidgetCopy->WidgetTree = Widget->WidgetTree ? DuplicateObject<UWidgetTree>(Widget->WidgetTree, WidgetCopy,
				MakeUniqueObjectName(WidgetCopy, UWidgetTree::StaticClass(), TEXT("PreviewTree"))) : nullptr;
		}
		Result = Blueprint;
	}
	else Result = DuplicateObject(Asset, GetTransientPackage());
	if (Result) Result->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
	if (Result) ForEachObjectWithOuter(Result, [](UObject* Child) { Child->ClearFlags(RF_Public | RF_Standalone | RF_Transactional); });
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Result))
	{
		// 结构预览从不编译；隔离副本持有的生成类，避免触及原资产 CDO。
		Blueprint->GeneratedClass = nullptr; Blueprint->SkeletonGeneratedClass = nullptr;
	}
	return Result;
}

bool FNexusAssetStructureUtils::ApplyPreview(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Op, const TSharedPtr<FJsonObject>& Entry, FString& Error)
{
	const FNexusArgs A(Op); const FString Action = A.Str(TEXT("Action"));
	if (!Supports(Capability, Action)) { Error = TEXT("Unsupported structural preview action"); return false; }
	if (Capability == TEXT("ManageAssetUserWidget"))
	{
		UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset);
		if (!Blueprint || !Blueprint->WidgetTree) { Error = TEXT("Widget tree is unavailable"); return false; }
		UWidgetTree* Tree = Blueprint->WidgetTree;
		const FString Name = A.Str(TEXT("WidgetName"));
		UWidget* Widget = Tree->FindWidget(FName(*Name));
		UPanelWidget* Parent = Cast<UPanelWidget>(Tree->FindWidget(FName(*A.Str(TEXT("ParentWidget")))));
		if (Action == TEXT("Add"))
		{
			FText NameError; UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(A.Str(TEXT("WidgetClass")));
			if (Widget || Name.IsEmpty() || !FName(*Name).IsValidObjectName(NameError) || !Class || !Class->IsChildOf(UWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("Add requires a unique valid WidgetName and concrete WidgetClass"); return false; }
			if ((!A.Str(TEXT("ParentWidget")).IsEmpty() && !Parent) || (!Parent && Tree->RootWidget) || (Parent && Parent->IsA(UContentWidget::StaticClass()) && Parent->GetChildrenCount())) { Error = TEXT("A free root or valid unoccupied parent panel is required"); return false; }
			Widget = Tree->ConstructWidget<UWidget>(Class, FName(*Name));
			if (!Widget || (Parent && !Parent->AddChild(Widget))) { Error = TEXT("Parent rejected new widget"); return false; }
			if (!Parent) Tree->RootWidget = Widget;
		}
		else
		{
			if (!Widget) { Error = TEXT("WidgetName not found"); return false; }
			if (Action == TEXT("Remove")) { if (Widget == Tree->RootWidget) Tree->RootWidget = nullptr; else Widget->RemoveFromParent(); }
			else
			{
				const int32 Index = static_cast<int32>(A.Num(TEXT("ChildIndex"), -1));
				if (!Parent || Index < -1 || Index > Parent->GetChildrenCount()) { Error = TEXT("Move requires a valid destination panel and child index"); return false; }
				for (UWidget* Ancestor = Parent; Ancestor; Ancestor = Ancestor->GetParent()) if (Ancestor == Widget) { Error = TEXT("Move would create a cycle"); return false; }
				if (Parent->IsA(UContentWidget::StaticClass()) && Parent->GetChildrenCount() && Widget->GetParent() != Parent) { Error = TEXT("Destination panel is occupied"); return false; }
				if (Widget->GetParent() == Parent) Parent->ShiftChild(Index < 0 ? Parent->GetChildrenCount() - 1 : FMath::Min(Index, Parent->GetChildrenCount() - 1), Widget);
				else { if (Tree->RootWidget == Widget) Tree->RootWidget = nullptr; Widget->RemoveFromParent(); if (!Parent->AddChild(Widget)) { Error = TEXT("Destination rejected widget"); return false; } if (Index >= 0) Parent->ShiftChild(Index, Widget); }
			}
		}
		return true;
	}
	if (Capability == TEXT("ManageAssetBlueprint"))
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		UEdGraph* Graph = Blueprint ? FNexusBlueprintGraphUtils::FindBPGraph(Blueprint, A.Str(TEXT("GraphName"))) : nullptr;
		if (!Graph || !Graph->GetSchema()) { Error = TEXT("GraphName not found"); return false; }
		if (Action == TEXT("RemoveNode"))
		{
			UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, A.Str(TEXT("NodeId")));
			if (!Node) { Error = TEXT("NodeId not found"); return false; }
			Graph->GetSchema()->BreakNodeLinks(*Node); Graph->RemoveNode(Node); return true;
		}
		UEdGraphNode* Source = FNexusBlueprintGraphUtils::FindBPNode(Graph, A.Str(TEXT("SourceNodeId")));
		UEdGraphPin* Pin = Source ? FNexusBlueprintGraphUtils::FindBPPin(Source, A.Str(TEXT("SourcePinName"))) : nullptr;
		if (!Pin) { Error = TEXT("Source pin not found"); return false; }
		if (Action == TEXT("DisconnectAll")) { Graph->GetSchema()->BreakPinLinks(*Pin, false); return true; }
		UEdGraphNode* Target = FNexusBlueprintGraphUtils::FindBPNode(Graph, A.Str(TEXT("TargetNodeId")));
		UEdGraphPin* Other = Target ? FNexusBlueprintGraphUtils::FindBPPin(Target, A.Str(TEXT("TargetPinName"))) : nullptr;
		if (!Other) { Error = TEXT("Target pin not found"); return false; }
		Graph->GetSchema()->BreakSinglePinLink(Pin, Other); return true;
	}
#if WITH_PCG
	if (UPCGGraph* Graph = Cast<UPCGGraph>(Asset))
	{
		const auto Find = [&](const FString& Name) -> UPCGNode* { if (Graph->GetInputNode() && Name == Graph->GetInputNode()->GetName()) return Graph->GetInputNode(); if (Graph->GetOutputNode() && Name == Graph->GetOutputNode()->GetName()) return Graph->GetOutputNode(); for (UPCGNode* Node : Graph->GetNodes()) if (Node && Name == Node->GetName()) return Node; return nullptr; };
		if (Action == TEXT("AddNode"))
		{
			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(A.Str(TEXT("SettingsClass")));
			if (!Class || !Class->IsChildOf(UPCGSettings::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("Concrete PCG SettingsClass is required"); return false; }
			UPCGNode* Node = Graph->AddNode(NewObject<UPCGSettings>(Graph, Class));
			if (!Node) { Error = TEXT("Could not add PCG node"); return false; }
			Entry->SetStringField(TEXT("NodeId"), Node->GetName()); return true;
		}
		if (Action == TEXT("RemoveNode")) { UPCGNode* Node = Find(A.Str(TEXT("NodeId"))); if (!Node || Node == Graph->GetInputNode() || Node == Graph->GetOutputNode()) { Error = TEXT("Removable NodeId not found"); return false; } Graph->RemoveNode(Node); return true; }
		UPCGNode* From = Find(A.Str(TEXT("FromNodeId"))); UPCGNode* To = Find(A.Str(TEXT("ToNodeId")));
		if (!From || !To) { Error = TEXT("Edge endpoint not found"); return false; }
		const auto Outputs = From->OutputPinProperties(), Inputs = To->InputPinProperties();
		const FName FromLabel = A.Str(TEXT("FromPin")).IsEmpty() && Outputs.Num() ? Outputs[0].Label : FName(*A.Str(TEXT("FromPin")));
		const FName ToLabel = A.Str(TEXT("ToPin")).IsEmpty() && Inputs.Num() ? Inputs[0].Label : FName(*A.Str(TEXT("ToPin")));
		if (!From->GetOutputPin(FromLabel) || !To->GetInputPin(ToLabel)) { Error = TEXT("Edge pin not found"); return false; }
		if (Action == TEXT("AddEdge")) { if (!Graph->AddEdge(From, FromLabel, To, ToLabel)) { Error = TEXT("PCG rejected edge"); return false; } }
		else Graph->RemoveEdge(From, FromLabel, To, ToLabel);
		return true;
	}
#endif
	Error = TEXT("Unsupported asset type"); return false;
}

void FNexusAssetStructureUtils::Wrap(const FString& Capability, TMap<FString, FNexusActionHandler>& Handlers, TFunction<UObject*(FNexusActionContext&)> Target)
{
	for (auto& Pair : Handlers)
	{
		if (!Supports(Capability, Pair.Key)) continue;
		const auto Handler = Pair.Value;
		Pair.Value = [Capability, Handler, Target](const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Context)
		{
			if (!FNexusArgs(Context.Args).Bool(TEXT("bVerifyStructure"))) { Handler(Op, Context); return; }
			UObject* Asset = Target(Context); UObject* Preview = Copy(Asset);
			auto Plan = MakeShared<FJsonObject>(); FString Error;
			if (!Preview || !ApplyPreview(Preview, Capability, Op, Plan, Error)) { Context.Entry->SetStringField(TEXT("Error"), Error.IsEmpty() ? TEXT("Could not create structural plan") : Error); return; }
			const auto Expected = Snapshot(Preview, FNexusArgs(Plan).Str(TEXT("NodeId")));
			Handler(Op, Context);
			if (Context.Entry->HasField(TEXT("Error"))) return;
			const bool bVerified = Expected == Snapshot(Asset, FNexusArgs(Plan).Str(TEXT("NodeId")).IsEmpty() ? FString() : FNexusArgs(Context.Entry).Str(TEXT("NodeId")));
			Context.Entry->SetBoolField(TEXT("bStructureVerified"), bVerified);
			if (!bVerified) Context.Entry->SetStringField(TEXT("Error"), TEXT("Structural readback differs from preview; changes remain unsaved"));
		};
	}
}
