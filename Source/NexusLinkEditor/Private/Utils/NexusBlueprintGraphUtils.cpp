// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusBlueprintGraphUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusAssetUtils.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "UObject/UObjectIterator.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"

void FNexusBlueprintGraphUtils::CollectAllGraphs(UBlueprint* BP, TArray<UEdGraph*>& OutGraphs)
{
	OutGraphs.Empty();
	if (!BP) return;
	// GetObjectsWithOuter 递归覆盖所有层级子图（含嵌套 StateMachine/State/Transition 等）
	TArray<UObject*> SubObjects;
#if NX_UE_HAS_GET_OBJECTS_FLAGS_ENUM
	GetObjectsWithOuter(BP, SubObjects, EGetObjectsFlags::IncludeNestedObjects);
#else
	GetObjectsWithOuter(BP, SubObjects, true);
#endif
	for (UObject* Obj : SubObjects)
	{
		if (UEdGraph* G = Cast<UEdGraph>(Obj))
			OutGraphs.AddUnique(G);
	}
}

UEdGraph* FNexusBlueprintGraphUtils::FindBPGraph(UBlueprint* BP, const FString& GraphName)
{
	TArray<UEdGraph*> AllGraphs;
	CollectAllGraphs(BP, AllGraphs);
	for (UEdGraph* G : AllGraphs)
		if (G && G->GetName() == GraphName) return G;
	return nullptr;
}

UEdGraphNode* FNexusBlueprintGraphUtils::FindBPNode(UEdGraph* Graph, const FString& NodeIdStr)
{
	if (!Graph) return nullptr;
	FGuid Guid;
	if (!FGuid::Parse(NodeIdStr, Guid)) return nullptr;
	for (UEdGraphNode* N : Graph->Nodes)
		if (N && N->NodeGuid == Guid) return N;
	return nullptr;
}

UEdGraphPin* FNexusBlueprintGraphUtils::FindBPPin(UEdGraphNode* Node, const FString& PinName)
{
	if (!Node || PinName.IsEmpty()) return nullptr;
	const FString PinLower = PinName.ToLower();
	// 精确匹配 PinName
	for (UEdGraphPin* P : Node->Pins)
		if (P && P->PinName.ToString() == PinName) return P;
	// 不区分大小写匹配 PinName
	for (UEdGraphPin* P : Node->Pins)
		if (P && P->PinName.ToString().ToLower() == PinLower) return P;
	// 匹配 PinFriendlyName（GetAssetBlueprint 会暴露，AI 常误用显示名）
	for (UEdGraphPin* P : Node->Pins)
	{
		if (!P) continue;
		const FString Friendly = P->PinFriendlyName.ToString();
		if (!Friendly.IsEmpty() && Friendly.ToLower() == PinLower) return P;
	}
	return nullptr;
}

FString FNexusBlueprintGraphUtils::FormatBPPinNameHint(UEdGraphNode* Node, int32 MaxNames)
{
	if (!Node || MaxNames <= 0) return FString();
	TArray<FString> Names;
	for (const UEdGraphPin* P : Node->Pins)
	{
		if (!P) continue;
		Names.AddUnique(P->PinName.ToString());
		if (Names.Num() >= MaxNames) break;
	}
	if (Names.Num() >= MaxNames && Node->Pins.Num() > MaxNames)
	{
		Names.Add(FString::Printf(TEXT("...+%d more"), Node->Pins.Num() - MaxNames));
	}
	return FString::Join(Names, TEXT(", "));
}

FString FNexusBlueprintGraphUtils::PinDirectionToString(EEdGraphPinDirection Dir)
{
	switch (Dir)
	{
	case EGPD_Input:  return TEXT("input");
	case EGPD_Output: return TEXT("Output");
	default:          return TEXT("unknown");
	}
}

TSharedPtr<FJsonObject> FNexusBlueprintGraphUtils::SerializeBPPin(const UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("PinName"),     Pin->PinName.ToString());
	P->SetStringField(TEXT("Direction"),   PinDirectionToString(Pin->Direction));
	P->SetStringField(TEXT("PinCategory"), Pin->PinType.PinCategory.ToString());
	const FString FN = Pin->PinFriendlyName.ToString();
	if (!FN.IsEmpty() && FN != Pin->PinName.ToString()) P->SetStringField(TEXT("PinFriendlyName"), FN);
	if (!Pin->PinType.PinSubCategory.IsNone()) P->SetStringField(TEXT("PinSubCategory"), Pin->PinType.PinSubCategory.ToString());
	if (Pin->PinType.PinSubCategoryObject.IsValid()) P->SetStringField(TEXT("PinSubCategoryObject"), Pin->PinType.PinSubCategoryObject->GetName());
	if (Pin->PinType.IsArray())         P->SetStringField(TEXT("ContainerType"), TEXT("array"));
	else if (Pin->PinType.IsMap())      P->SetStringField(TEXT("ContainerType"), TEXT("map"));
	else if (Pin->PinType.IsSet())      P->SetStringField(TEXT("ContainerType"), TEXT("Set"));
	else                               P->SetStringField(TEXT("ContainerType"), TEXT("None"));
	P->SetBoolField(TEXT("bIsReference"), Pin->PinType.bIsReference);
	P->SetBoolField(TEXT("bIsConst"),     Pin->PinType.bIsConst);
	if (!Pin->DefaultValue.IsEmpty()) P->SetStringField(TEXT("DefaultValue"), Pin->DefaultValue);
	if (Pin->DefaultObject) P->SetStringField(TEXT("DefaultObject"), Pin->DefaultObject->GetPathName());
	P->SetBoolField(TEXT("bOrphan"), Pin->bOrphanedPin);
	if (Pin->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* LP : Pin->LinkedTo)
		{
			if (!LP || !LP->GetOwningNode()) continue;
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("NodeId"),  LP->GetOwningNode()->NodeGuid.ToString());
			L->SetStringField(TEXT("PinId"),   LP->PinId.ToString());
			L->SetStringField(TEXT("PinName"), LP->PinName.ToString());
			Links.Add(MakeShared<FJsonValueObject>(L));
		}
		P->SetArrayField(TEXT("LinkedTo"), Links);
	}
	return P;
}

TSharedPtr<FJsonObject> FNexusBlueprintGraphUtils::SerializeBPNode(const UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
	N->SetStringField(TEXT("NodeId"),    Node->NodeGuid.ToString());
	N->SetStringField(TEXT("NodeClass"), Node->GetClass()->GetName());
	N->SetStringField(TEXT("NodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	if (!Node->NodeComment.IsEmpty()) N->SetStringField(TEXT("Comment"), Node->NodeComment);
	N->SetBoolField(TEXT("bIsNodeEnabled"), Node->IsNodeEnabled());
	TArray<TSharedPtr<FJsonValue>> Pins;
	for (const UEdGraphPin* Pin : Node->Pins)
		if (Pin) Pins.Add(MakeShared<FJsonValueObject>(SerializeBPPin(Pin)));
	N->SetArrayField(TEXT("Pins"), Pins);
	return N;
}

FString FNexusBlueprintGraphUtils::GetBPGraphType(const UEdGraph* Graph)
{
	if (!Graph) return TEXT("unknown");
	const FString CN = Graph->GetClass()->GetName();
	if (CN.Contains(TEXT("AnimationStateMachineGraph"))) return TEXT("Statemachine");
	if (CN.Contains(TEXT("AnimStateTransitionGraph")))   return TEXT("Transition");
	if (CN.Contains(TEXT("AnimStateGraph")))             return TEXT("State");
	if (CN.Contains(TEXT("AnimationConduitGraph")))      return TEXT("Conduit");
	if (CN.Contains(TEXT("AnimationGraph")))             return TEXT("Animgraph");
	if (CN.Contains(TEXT("EventGraph")) || Graph->GetName() == TEXT("EventGraph")) return TEXT("Event");
	if (CN.Contains(TEXT("MacroGraph")))                 return TEXT("Macro");
	return TEXT("Function");
}

FString FNexusBlueprintGraphUtils::GetBPParentGraphName(const UEdGraph* Graph)
{
	if (!Graph) return FString();
	if (UEdGraphNode* OwnerNode = Cast<UEdGraphNode>(Graph->GetOuter()))
		if (UEdGraph* OwnerGraph = OwnerNode->GetGraph())
			return OwnerGraph->GetName();
	return FString();
}

TSharedPtr<FJsonObject> FNexusBlueprintGraphUtils::BuildBPGraphSummary(const UEdGraph* Graph)
{
	TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
	GObj->SetStringField(TEXT("GraphName"), Graph->GetName());
	GObj->SetStringField(TEXT("GraphType"), GetBPGraphType(Graph));
	int32 En = 0, Dis = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Node->IsNodeEnabled()) ++En; else ++Dis;
	}
	GObj->SetNumberField(TEXT("EnabledNodeCount"),  En);
	GObj->SetNumberField(TEXT("DisabledNodeCount"), Dis);
	const FString Parent = GetBPParentGraphName(Graph);
	if (!Parent.IsEmpty()) GObj->SetStringField(TEXT("ParentGraph"), Parent);
	return GObj;
}

void FNexusBlueprintGraphUtils::CollectOrphanedPins(UBlueprint* BP, TArray<TSharedPtr<FJsonObject>>& OutPins, int32 MaxCount)
{
	OutPins.Reset();
	if (!BP || MaxCount <= 0) return;
	TArray<UEdGraph*> Graphs;
	CollectAllGraphs(BP, Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || !Pin->bOrphanedPin) continue;
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("NodeId"), Node->NodeGuid.ToString());
				E->SetStringField(TEXT("PinName"), Pin->PinName.ToString());
				OutPins.Add(E);
				if (OutPins.Num() >= MaxCount) return;
			}
		}
	}
}

static bool IsExecPin(const UEdGraphPin* Pin)
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

void FNexusBlueprintGraphUtils::CollectExecPaths(UEdGraph* Graph, TArray<TSharedPtr<FJsonObject>>& OutPaths,
	int32 MaxPaths, int32 MaxNodes)
{
	OutPaths.Reset();
	if (!Graph || MaxPaths <= 0 || MaxNodes <= 0) return;
	for (UEdGraphNode* Start : Graph->Nodes)
	{
		if (!Start || !Start->IsA(UK2Node_Event::StaticClass())) continue;
		if (OutPaths.Num() >= MaxPaths) break;

		TArray<TSharedPtr<FJsonValue>> NodesArr;
		TSet<const UEdGraphNode*> Visited;
		UEdGraphNode* Cur = Start;
		while (Cur && NodesArr.Num() < MaxNodes && !Visited.Contains(Cur))
		{
			Visited.Add(Cur);
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("NodeId"), Cur->NodeGuid.ToString());
			N->SetStringField(TEXT("NodeTitle"), Cur->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodesArr.Add(MakeShared<FJsonValueObject>(N));

			UEdGraphNode* Next = nullptr;
			for (UEdGraphPin* Pin : Cur->Pins)
			{
				if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) continue;
				if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0] && Pin->LinkedTo[0]->GetOwningNode())
				{
					Next = Pin->LinkedTo[0]->GetOwningNode();
					break;
				}
			}
			Cur = Next;
		}

		TSharedPtr<FJsonObject> PathObj = MakeShared<FJsonObject>();
		PathObj->SetStringField(TEXT("Start"), Start->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		PathObj->SetArrayField(TEXT("Nodes"), NodesArr);
		OutPaths.Add(PathObj);
	}
}

// ── 写侧：AddNode 各分支共用的节点创建 + 放置 ─────────────────────────────────

void FNexusBlueprintGraphUtils::PlaceNewNode(UEdGraph* Graph, UEdGraphNode* Node, int32 PosX, int32 PosY)
{
	if (!Graph || !Node) return;
	Graph->AddNode(Node, false, false);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Node->NodePosX = PosX;
	Node->NodePosY = PosY;
}

UFunction* FNexusBlueprintGraphUtils::ResolveGraphFunction(const FString& FuncName, const FString& FuncClassName)
{
	if (!FuncClassName.IsEmpty())
	{
		if (UClass* Owner = FNexusAssetUtils::FindClassWithUPrefix(FuncClassName))
		{
			if (UFunction* Func = Owner->FindFunctionByName(*FuncName))
			{
				return Func;
			}
		}
	}
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (UFunction* Func = It->FindFunctionByName(*FuncName))
		{
			return Func;
		}
	}
	const FString K2Name = TEXT("K2_") + FuncName;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (UFunction* Func = It->FindFunctionByName(*K2Name))
		{
			return Func;
		}
	}
	return nullptr;
}

UEdGraphNode* FNexusBlueprintGraphUtils::MakeCallFunctionNode(UEdGraph* Graph, UFunction* Func, int32 PosX, int32 PosY)
{
	if (!Graph || !Func) return nullptr;
	UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
	Node->SetFlags(RF_Transactional);
	Node->SetFromFunction(Func);
	PlaceNewNode(Graph, Node, PosX, PosY);
	return Node;
}

UEdGraphNode* FNexusBlueprintGraphUtils::MakeEventNode(UBlueprint* BP, UEdGraph* Graph, const FString& EventName, UClass* EventClass, int32 PosX, int32 PosY)
{
	if (!BP || !Graph || !EventClass) return nullptr;
	if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(BP, EventClass, FName(*EventName)))
	{
		Existing->SetEnabledState(ENodeEnabledState::Enabled, true);
		return Existing;
	}
	UK2Node_Event* Node = NewObject<UK2Node_Event>(Graph);
	Node->SetFlags(RF_Transactional);
	Node->EventReference.SetExternalMember(FName(*EventName), EventClass);
	Node->bOverrideFunction = true;
	PlaceNewNode(Graph, Node, PosX, PosY);
	return Node;
}

UEdGraphNode* FNexusBlueprintGraphUtils::MakeVariableNode(UEdGraph* Graph, const FString& VarName, bool bSetter, int32 PosX, int32 PosY)
{
	if (!Graph) return nullptr;
	if (bSetter)
	{
		UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(Graph);
		Node->SetFlags(RF_Transactional);
		Node->VariableReference.SetSelfMember(FName(*VarName));
		PlaceNewNode(Graph, Node, PosX, PosY);
		return Node;
	}
	UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
	Node->SetFlags(RF_Transactional);
	Node->VariableReference.SetSelfMember(FName(*VarName));
	PlaceNewNode(Graph, Node, PosX, PosY);
	return Node;
}

UEdGraphNode* FNexusBlueprintGraphUtils::MakeGenericNode(UEdGraph* Graph, UClass* NodeClass, int32 PosX, int32 PosY)
{
	if (!Graph || !NodeClass) return nullptr;
	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass);
	Node->SetFlags(RF_Transactional);
	PlaceNewNode(Graph, Node, PosX, PosY);
	return Node;
}

#endif // WITH_EDITOR
