// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusAnimGraphUtils.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNodeBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"

UEdGraph* FNexusAnimGraphUtils::FindAnimGraph(UAnimBlueprint* BP, const FString& GraphName)
{
	if (!BP) return nullptr;

	const FString WantName = GraphName.IsEmpty() ? TEXT("AnimGraph") : GraphName;
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G && G->GetName() == WantName)
		{
			return G;
		}
	}
	return nullptr;
}

UAnimGraphNode_StateMachineBase* FNexusAnimGraphUtils::FindStateMachineNode(UEdGraph* AnimGraph, const FString& NodeName)
{
	if (!AnimGraph || NodeName.IsEmpty()) return nullptr;

	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
		if (!SMNode) continue;

		// 优先匹配 BoundGraph 名（用户传入的状态机名通常等于其子图名）
		if (SMNode->EditorStateMachineGraph && SMNode->EditorStateMachineGraph->GetName() == NodeName)
		{
			return SMNode;
		}
		if (SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString() == NodeName)
		{
			return SMNode;
		}
	}
	return nullptr;
}

UAnimationStateMachineGraph* FNexusAnimGraphUtils::GetStateMachineGraph(UAnimGraphNode_StateMachineBase* SMNode)
{
	if (!SMNode) return nullptr;
	return Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
}

UAnimStateNode* FNexusAnimGraphUtils::FindStateByName(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	if (!SMGraph || StateName.IsEmpty()) return nullptr;

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateNode* State = Cast<UAnimStateNode>(Node);
		if (!State) continue;

		// 优先匹配 BoundGraph 名（创建时通过 graphName 命名），再回退 NodeName
		if (State->BoundGraph && State->BoundGraph->GetName() == StateName)
		{
			return State;
		}
		if (State->GetStateName() == StateName)
		{
			return State;
		}
	}
	return nullptr;
}

UAnimStateTransitionNode* FNexusAnimGraphUtils::FindTransition(UAnimationStateMachineGraph* SMGraph,
                                                                const FString& SourceStateName,
                                                                const FString& TargetStateName)
{
	if (!SMGraph) return nullptr;

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(Node);
		if (!Trans) continue;

		UAnimStateNodeBase* PrevState = Trans->GetPreviousState();
		UAnimStateNodeBase* NextState = Trans->GetNextState();
		if (!PrevState || !NextState) continue;

		const FString PrevName = PrevState->GetStateName();
		const FString NextName = NextState->GetStateName();
		if (PrevName == SourceStateName && NextName == TargetStateName)
		{
			return Trans;
		}
	}
	return nullptr;
}

UEdGraphPin* FNexusAnimGraphUtils::GetStateOutputPin(UAnimStateNodeBase* StateNode)
{
	if (!StateNode) return nullptr;
	// UAnimStateNodeBase::GetOutputPin() 自 UE4.20+ 稳定
	return StateNode->GetOutputPin();
}

UEdGraphPin* FNexusAnimGraphUtils::GetStateInputPin(UAnimStateNodeBase* StateNode)
{
	if (!StateNode) return nullptr;
	return StateNode->GetInputPin();
}

#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_Base.h"

UClass* FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(const FString& NodeClass)
{
	FString Name = NodeClass;
	Name.ReplaceInline(TEXT("U"), TEXT(""));
	if (Name.StartsWith(TEXT("AnimGraphNode_")))
	{
		Name = Name.Mid(14);
	}
	if (Name.Equals(TEXT("SequencePlayer"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_SequencePlayer::StaticClass();
	}
	if (Name.Equals(TEXT("BlendSpacePlayer"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_BlendSpacePlayer::StaticClass();
	}
	if (Name.Equals(TEXT("Slot"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_Slot::StaticClass();
	}
	return nullptr;
}

UEdGraphNode* FNexusAnimGraphUtils::FindNodeByGuidOrTitle(UEdGraph* Graph, const FString& GuidOrTitle)
{
	if (!Graph || GuidOrTitle.IsEmpty()) return nullptr;
	FGuid Parsed;
	const bool bHasGuid = FGuid::Parse(GuidOrTitle, Parsed);
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (bHasGuid && Node->NodeGuid == Parsed) return Node;
		if (Node->GetName() == GuidOrTitle) return Node;
		if (Node->GetNodeTitle(ENodeTitleType::ListView).ToString() == GuidOrTitle) return Node;
	}
	return nullptr;
}

UEdGraphNode* FNexusAnimGraphUtils::SpawnAnimGraphNode(UEdGraph* Graph, UClass* NodeClass, int32 PosX, int32 PosY, FString& OutError)
{
	if (!Graph) { OutError = TEXT("AnimGraph 无效"); return nullptr; }
	if (!NodeClass || !NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass()))
	{
		OutError = TEXT("仅支持 AnimGraph 节点（SequencePlayer/BlendSpacePlayer/Slot）");
		return nullptr;
	}
	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass);
	if (!Node) { OutError = TEXT("创建 AnimGraph 节点失败"); return nullptr; }
	Node->CreateNewGuid();
	Node->NodePosX = PosX;
	Node->NodePosY = PosY;
	Graph->AddNode(Node, /*bFromUI*/false, /*bSelectNewNode*/false);
	Node->AllocateDefaultPins();
	return Node;
}

static UEdGraphPin* FindAnimPinByName(UEdGraphNode* Node, const FString& PinName)
{
	if (!Node || PinName.IsEmpty()) return nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;
		if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)) return Pin;
		if (Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)) return Pin;
	}
	return nullptr;
}

bool FNexusAnimGraphUtils::ConnectAnimPins(UEdGraphNode* Source, const FString& SourcePin,
	UEdGraphNode* Target, const FString& TargetPin, FString& OutError)
{
	if (!Source || !Target) { OutError = TEXT("源或目标节点无效"); return false; }
	UEdGraphPin* Src = FindAnimPinByName(Source, SourcePin);
	UEdGraphPin* Dst = FindAnimPinByName(Target, TargetPin);
	if (!Src) { OutError = FString::Printf(TEXT("源引脚未找到: %s"), *SourcePin); return false; }
	if (!Dst) { OutError = FString::Printf(TEXT("目标引脚未找到: %s"), *TargetPin); return false; }
	if (Src->Direction == Dst->Direction)
	{
		OutError = TEXT("引脚方向相同，无法连接");
		return false;
	}
	if (Src->Direction == EGPD_Input)
	{
		Swap(Src, Dst);
	}
	Src->MakeLinkTo(Dst);
	return true;
}

#endif // WITH_EDITOR
