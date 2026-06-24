// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusBlueprintGraphUtils.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "UObject/UObjectIterator.h"

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
	// 匹配 PinFriendlyName（get_asset_blueprint 会暴露，AI 常误用显示名）
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
	case EGPD_Output: return TEXT("output");
	default:          return TEXT("unknown");
	}
}

TSharedPtr<FJsonObject> FNexusBlueprintGraphUtils::SerializeBPPin(const UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("pinName"),     Pin->PinName.ToString());
	P->SetStringField(TEXT("direction"),   PinDirectionToString(Pin->Direction));
	P->SetStringField(TEXT("pinCategory"), Pin->PinType.PinCategory.ToString());
	const FString FN = Pin->PinFriendlyName.ToString();
	if (!FN.IsEmpty() && FN != Pin->PinName.ToString()) P->SetStringField(TEXT("pinFriendlyName"), FN);
	if (!Pin->PinType.PinSubCategory.IsNone()) P->SetStringField(TEXT("pinSubCategory"), Pin->PinType.PinSubCategory.ToString());
	if (Pin->PinType.PinSubCategoryObject.IsValid()) P->SetStringField(TEXT("pinSubCategoryObject"), Pin->PinType.PinSubCategoryObject->GetName());
	if (Pin->PinType.IsArray())    P->SetStringField(TEXT("containerType"), TEXT("array"));
	else if (Pin->PinType.IsMap()) P->SetStringField(TEXT("containerType"), TEXT("map"));
	else if (Pin->PinType.IsSet()) P->SetStringField(TEXT("containerType"), TEXT("set"));
	if (Pin->PinType.bIsReference) P->SetBoolField(TEXT("isReference"), true);
	if (Pin->PinType.bIsConst)     P->SetBoolField(TEXT("isConst"),     true);
	if (!Pin->DefaultValue.IsEmpty()) P->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
	if (Pin->DefaultObject) P->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
	if (Pin->bOrphanedPin) P->SetBoolField(TEXT("bOrphan"), true);
	if (Pin->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* LP : Pin->LinkedTo)
		{
			if (!LP || !LP->GetOwningNode()) continue;
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("nodeId"),  LP->GetOwningNode()->NodeGuid.ToString());
			L->SetStringField(TEXT("pinId"),   LP->PinId.ToString());
			L->SetStringField(TEXT("pinName"), LP->PinName.ToString());
			Links.Add(MakeShared<FJsonValueObject>(L));
		}
		P->SetArrayField(TEXT("linkedTo"), Links);
	}
	return P;
}

TSharedPtr<FJsonObject> FNexusBlueprintGraphUtils::SerializeBPNode(const UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
	N->SetStringField(TEXT("nodeId"),    Node->NodeGuid.ToString());
	N->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
	N->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	if (!Node->NodeComment.IsEmpty()) N->SetStringField(TEXT("comment"), Node->NodeComment);
	if (!Node->IsNodeEnabled()) N->SetBoolField(TEXT("bIsNodeEnabled"), false);
	TArray<TSharedPtr<FJsonValue>> Pins;
	for (const UEdGraphPin* Pin : Node->Pins)
		if (Pin) Pins.Add(MakeShared<FJsonValueObject>(SerializeBPPin(Pin)));
	N->SetArrayField(TEXT("pins"), Pins);
	return N;
}

FString FNexusBlueprintGraphUtils::GetBPGraphType(const UEdGraph* Graph)
{
	if (!Graph) return TEXT("unknown");
	const FString CN = Graph->GetClass()->GetName();
	if (CN.Contains(TEXT("AnimationStateMachineGraph"))) return TEXT("statemachine");
	if (CN.Contains(TEXT("AnimStateTransitionGraph")))   return TEXT("transition");
	if (CN.Contains(TEXT("AnimStateGraph")))             return TEXT("state");
	if (CN.Contains(TEXT("AnimationConduitGraph")))      return TEXT("conduit");
	if (CN.Contains(TEXT("AnimationGraph")))             return TEXT("animgraph");
	if (CN.Contains(TEXT("EventGraph")) || Graph->GetName() == TEXT("EventGraph")) return TEXT("event");
	if (CN.Contains(TEXT("MacroGraph")))                 return TEXT("macro");
	return TEXT("function");
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
	GObj->SetStringField(TEXT("graphName"), Graph->GetName());
	GObj->SetStringField(TEXT("graphType"), GetBPGraphType(Graph));
	int32 En = 0, Dis = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Node->IsNodeEnabled()) ++En; else ++Dis;
	}
	GObj->SetNumberField(TEXT("enabledNodeCount"),  En);
	GObj->SetNumberField(TEXT("disabledNodeCount"), Dis);
	const FString Parent = GetBPParentGraphName(Graph);
	if (!Parent.IsEmpty()) GObj->SetStringField(TEXT("parentGraph"), Parent);
	return GObj;
}

#endif // WITH_EDITOR
