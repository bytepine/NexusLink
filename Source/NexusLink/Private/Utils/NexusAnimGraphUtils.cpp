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
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_BlendSpaceEvaluator.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_MultiWayBlend.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_RandomPlayer.h"
#include "AnimGraphNode_PoseBlendNode.h"
#include "AnimGraphNode_PoseByName.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_Fabrik.h"
#include "AnimGraphNode_CCDIK.h"
#include "AnimGraphNode_LookAt.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_HandIKRetargeting.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_AimOffsetLookAt.h"
#include "AnimGraphNode_Base.h"
#if WITH_CONTROL_RIG
#include "AnimGraphNode_ControlRig.h"
#endif

namespace NexusAnimGraphNodeNames
{
#if WITH_CONTROL_RIG
	static const TCHAR* SupportedList =
		TEXT("SequencePlayer/SequenceEvaluator/BlendSpacePlayer(=BlendSpace1D)/BlendSpaceEvaluator/RandomPlayer/"
			 "PoseBlendNode/PoseByName/Slot/Blend/BlendListByEnum/BlendListByInt/MultiWayBlend/LayeredBoneBlend/"
			 "ApplyAdditive/SaveCachedPose/UseCachedPose/Inertialization/ComponentToLocalSpace/LocalToComponentSpace/"
			 "TwoBoneIK/FABRIK/CCDIK/LookAt/ModifyBone/CopyBone/HandIKRetargeting/AimOffset/AimOffsetLookAt/ControlRig");
#else
	static const TCHAR* SupportedList =
		TEXT("SequencePlayer/SequenceEvaluator/BlendSpacePlayer(=BlendSpace1D)/BlendSpaceEvaluator/RandomPlayer/"
			 "PoseBlendNode/PoseByName/Slot/Blend/BlendListByEnum/BlendListByInt/MultiWayBlend/LayeredBoneBlend/"
			 "ApplyAdditive/SaveCachedPose/UseCachedPose/Inertialization/ComponentToLocalSpace/LocalToComponentSpace/"
			 "TwoBoneIK/FABRIK/CCDIK/LookAt/ModifyBone/CopyBone/HandIKRetargeting/AimOffset/AimOffsetLookAt");
#endif
}

UClass* FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(const FString& NodeClass)
{
	FString Name = NodeClass;
	// 只剥类名前缀，禁止全局删 'U'（否则 SequencePlayer→SeqencePlayer、UseCachedPose→seCachedPose）
	if (Name.StartsWith(TEXT("UAnimGraphNode_")))
	{
		Name = Name.Mid(15);
	}
	else if (Name.StartsWith(TEXT("AnimGraphNode_")))
	{
		Name = Name.Mid(14);
	}
	if (Name.Equals(TEXT("SequencePlayer"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_SequencePlayer::StaticClass();
	}
	if (Name.Equals(TEXT("SequenceEvaluator"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_SequenceEvaluator::StaticClass();
	}
	if (Name.Equals(TEXT("BlendSpacePlayer"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("BlendSpace1D"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("BlendSpacePlayer1D"), ESearchCase::IgnoreCase))
	{
		// BlendSpace / BlendSpace1D 共用 BlendSpacePlayer 节点
		return UAnimGraphNode_BlendSpacePlayer::StaticClass();
	}
	if (Name.Equals(TEXT("BlendSpaceEvaluator"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_BlendSpaceEvaluator::StaticClass();
	}
	if (Name.Equals(TEXT("RandomPlayer"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_RandomPlayer::StaticClass();
	}
	if (Name.Equals(TEXT("PoseBlendNode"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("PoseBlend"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_PoseBlendNode::StaticClass();
	}
	if (Name.Equals(TEXT("PoseByName"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_PoseByName::StaticClass();
	}
	if (Name.Equals(TEXT("Slot"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_Slot::StaticClass();
	}
	if (Name.Equals(TEXT("Blend"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("BlendListByBool"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_BlendListByBool::StaticClass();
	}
	if (Name.Equals(TEXT("BlendListByEnum"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_BlendListByEnum::StaticClass();
	}
	if (Name.Equals(TEXT("BlendListByInt"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_BlendListByInt::StaticClass();
	}
	if (Name.Equals(TEXT("MultiWayBlend"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_MultiWayBlend::StaticClass();
	}
	if (Name.Equals(TEXT("LayeredBoneBlend"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_LayeredBoneBlend::StaticClass();
	}
	if (Name.Equals(TEXT("ApplyAdditive"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_ApplyAdditive::StaticClass();
	}
	if (Name.Equals(TEXT("SaveCachedPose"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_SaveCachedPose::StaticClass();
	}
	if (Name.Equals(TEXT("UseCachedPose"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_UseCachedPose::StaticClass();
	}
	if (Name.Equals(TEXT("Inertialization"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_Inertialization::StaticClass();
	}
	if (Name.Equals(TEXT("ComponentToLocalSpace"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("MeshToLocal"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_ComponentToLocalSpace::StaticClass();
	}
	if (Name.Equals(TEXT("LocalToComponentSpace"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("LocalToMesh"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_LocalToComponentSpace::StaticClass();
	}
	if (Name.Equals(TEXT("TwoBoneIK"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("IK"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_TwoBoneIK::StaticClass();
	}
	if (Name.Equals(TEXT("FABRIK"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("Fabrik"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_Fabrik::StaticClass();
	}
	if (Name.Equals(TEXT("CCDIK"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_CCDIK::StaticClass();
	}
	if (Name.Equals(TEXT("LookAt"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_LookAt::StaticClass();
	}
	if (Name.Equals(TEXT("ModifyBone"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_ModifyBone::StaticClass();
	}
	if (Name.Equals(TEXT("CopyBone"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_CopyBone::StaticClass();
	}
	if (Name.Equals(TEXT("HandIKRetargeting"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("HandIK"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_HandIKRetargeting::StaticClass();
	}
	if (Name.Equals(TEXT("AimOffset"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("RotationOffsetBlendSpace"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_RotationOffsetBlendSpace::StaticClass();
	}
	if (Name.Equals(TEXT("AimOffsetLookAt"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_AimOffsetLookAt::StaticClass();
	}
#if WITH_CONTROL_RIG
	if (Name.Equals(TEXT("ControlRig"), ESearchCase::IgnoreCase))
	{
		return UAnimGraphNode_ControlRig::StaticClass();
	}
#endif
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
		OutError = FString::Printf(TEXT("仅支持 AnimGraph 节点（%s）"), NexusAnimGraphNodeNames::SupportedList);
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

void FNexusAnimGraphUtils::ApplyBoneName(UEdGraphNode* Node, const FString& BoneName)
{
	if (!Node || BoneName.IsEmpty()) return;
	const FName Bone(*BoneName);
	if (UAnimGraphNode_TwoBoneIK* IK = Cast<UAnimGraphNode_TwoBoneIK>(Node))
	{
		IK->Node.IKBone.BoneName = Bone;
	}
	else if (UAnimGraphNode_Fabrik* Fabrik = Cast<UAnimGraphNode_Fabrik>(Node))
	{
		Fabrik->Node.TipBone.BoneName = Bone;
	}
	else if (UAnimGraphNode_CCDIK* CCD = Cast<UAnimGraphNode_CCDIK>(Node))
	{
		CCD->Node.TipBone.BoneName = Bone;
	}
	else if (UAnimGraphNode_LookAt* LookAt = Cast<UAnimGraphNode_LookAt>(Node))
	{
		LookAt->Node.BoneToModify.BoneName = Bone;
	}
	else if (UAnimGraphNode_ModifyBone* Modify = Cast<UAnimGraphNode_ModifyBone>(Node))
	{
		Modify->Node.BoneToModify.BoneName = Bone;
	}
	else if (UAnimGraphNode_CopyBone* Copy = Cast<UAnimGraphNode_CopyBone>(Node))
	{
		Copy->Node.TargetBone.BoneName = Bone;
	}
}

#endif // WITH_EDITOR
