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

#endif // WITH_EDITOR
