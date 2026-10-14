// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimBlueprintCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAnimGraphUtils.h"
#include "Animation/AnimBlueprint.h"
#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#endif
#include "NexusMcpTool.h"

// ─── 共享辅助 ──────────────────────────────────────────────────────────────

#if WITH_EDITOR

/** 在指定 SMGraph 中查找连接 source 或 Target == state 的所有 transition 节点。 */
static void CollectTransitionsForState(UAnimationStateMachineGraph* SMGraph, UAnimStateNode* State,
                                       TArray<UAnimStateTransitionNode*>& OutTransitions)
{
	if (!SMGraph || !State) return;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(Node);
		if (!Trans) continue;
		if (Trans->GetPreviousState() == State || Trans->GetNextState() == State)
		{
			OutTransitions.Add(Trans);
		}
	}
}

/** 释放图节点：断 Pin、移出 Graph、移除 BoundGraph（如有）。 */
static void DestroyGraphNode(UEdGraphNode* Node)
{
	if (!Node) return;
	Node->BreakAllNodeLinks();
	if (UEdGraph* OwnerGraph = Node->GetGraph())
	{
		OwnerGraph->RemoveNode(Node);
	}
}

#endif // WITH_EDITOR

// ─── Capability ──────────────────────────────────────────────────────────────

void FNexusManageAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetAnimBlueprint");
	Out.bInjectCompile = true;
	Out.SearchAssetTypes = {TEXT("AnimBlueprint")};
	Out.Description = TEXT("Batch edit ABP. State machines and AnimGraph nodes; avoid K2.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),           FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("AddStateMachine"), TEXT("RemoveStateMachine"),
			  TEXT("AddState"),         TEXT("RemoveState"),
			  TEXT("AddTransition"),    TEXT("RemoveTransition"),
			  TEXT("AddNode"), TEXT("RemoveNode"), TEXT("SetNode"),
			  TEXT("Connect"), TEXT("Disconnect") }))
		.Prop(TEXT("GraphName"),        FNexusSchema::Str(TEXT("Owning AnimGraph name (default AnimGraph)")))
		.Prop(TEXT("StateMachineName"), FNexusSchema::Str(TEXT("State machine name (bound graph)")))
		.Prop(TEXT("StateName"),        FNexusSchema::Str(TEXT("State name (add/RemoveState, transition source)")))
		.Prop(TEXT("TargetStateName"),  FNexusSchema::Str(TEXT("Transition target state name")))
		.Prop(TEXT("NodeClass"),        FNexusSchema::Enum(TEXT("AnimGraph node class (AddNode)"),
			{ TEXT("SequencePlayer"), TEXT("SequenceEvaluator"),
			  TEXT("BlendSpacePlayer"), TEXT("BlendSpace1D"), TEXT("BlendSpaceEvaluator"),
			  TEXT("RandomPlayer"), TEXT("PoseBlendNode"), TEXT("PoseByName"),
			  TEXT("Slot"), TEXT("Blend"), TEXT("BlendListByEnum"), TEXT("BlendListByInt"),
			  TEXT("MultiWayBlend"), TEXT("LayeredBoneBlend"), TEXT("ApplyAdditive"),
			  TEXT("SaveCachedPose"), TEXT("UseCachedPose"), TEXT("Inertialization"),
			  TEXT("ComponentToLocalSpace"), TEXT("LocalToComponentSpace"),
			  TEXT("TwoBoneIK"), TEXT("FABRIK"), TEXT("CCDIK"),
			  TEXT("LookAt"), TEXT("ModifyBone"), TEXT("CopyBone"), TEXT("HandIKRetargeting"),
			  TEXT("AimOffset"), TEXT("AimOffsetLookAt"), TEXT("ControlRig") }))
		.Prop(TEXT("NodeId"),           FNexusSchema::Str(TEXT("node GUID (remove/SetNode/connect)")))
		.Prop(TEXT("SequencePath"),     FNexusSchema::Str(TEXT("Animation asset path (SetNode Sequence/BlendSpace/AimOffset)")))
		.Prop(TEXT("SlotName"),         FNexusSchema::Str(TEXT("Slot name (SetNode Slot)")))
		.Prop(TEXT("BoneName"),         FNexusSchema::Str(TEXT("Bone name (TwoBoneIK=IKBone; FABRIK/CCDIK=TipBone; LookAt/ModifyBone; CopyBone=TargetBone)")))
		.Prop(TEXT("SourceNodeId"),     FNexusSchema::Str(TEXT("Source node GUID (connect/disconnect)")))
		.Prop(TEXT("SourcePinName"),    FNexusSchema::Str(TEXT("Source pin name")))
		.Prop(TEXT("TargetNodeId"),     FNexusSchema::Str(TEXT("Target node GUID")))
		.Prop(TEXT("TargetPinName"),    FNexusSchema::Str(TEXT("Target pin name")))
		.Prop(TEXT("PosX"),             FNexusSchema::Num(TEXT("Editor node X (optional)")))
		.Prop(TEXT("PosY"),             FNexusSchema::Num(TEXT("Editor node Y (optional)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("AnimBlueprint asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("abp"), TEXT("Statemachine"), TEXT("State"), TEXT("Transition"), TEXT("Animgraph")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetAnimBlueprint"), TEXT("CreateAssetAnimBlueprint"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("State machine and AnimGraph node CRUD; do not use ManageAssetBlueprint for AnimGraph");
}

#if WITH_EDITOR
struct FAnimBPActionState
{
	UAnimBlueprint* AnimBP = nullptr;
	bool bModified = false;
};

static FAnimBPActionState* AnimState(FNexusActionContext& Ctx)
{
	return static_cast<FAnimBPActionState*>(Ctx.Target);
}

static UAnimBlueprint* AnimBPFrom(FNexusActionContext& Ctx)
{
	FAnimBPActionState* S = AnimState(Ctx);
	return S ? S->AnimBP : nullptr;
}

static void MarkAnimModified(FNexusActionContext& Ctx)
{
	if (FAnimBPActionState* S = AnimState(Ctx))
	{
		S->bModified = true;
	}
}

static FString GraphNameOf(const TSharedPtr<FJsonObject>& Op)
{
	return FNexusArgs(Op).Str(TEXT("GraphName"));
}

static int32 PosXOf(const TSharedPtr<FJsonObject>& Op)
{
	return static_cast<int32>(FNexusArgs(Op).Num(TEXT("PosX"), 0.0));
}

static int32 PosYOf(const TSharedPtr<FJsonObject>& Op)
{
	return static_cast<int32>(FNexusArgs(Op).Num(TEXT("PosY"), 0.0));
}

static void HandleABP_AddStateMachine(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("StateMachineName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddStateMachine requires StateMachineName"));
		return;
	}
	const FString GraphName = GraphNameOf(Op);
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
	if (!AnimGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("AnimGraph '%s' not found in AnimBlueprint"), GraphName.IsEmpty() ? TEXT("AnimGraph") : *GraphName));
		return;
	}
	if (FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State machine '%s' already exists"), *SMName));
		return;
	}
	UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
	SMNode->CreateNewGuid();
	SMNode->NodePosX = PosXOf(Op);
	SMNode->NodePosY = PosYOf(Op);
	AnimGraph->AddNode(SMNode, /*bFromUI*/false, /*bSelectNewNode*/false);
	UEdGraph* SMGraph = FBlueprintEditorUtils::CreateNewGraph(
		SMNode, FName(*SMName),
		UAnimationStateMachineGraph::StaticClass(),
		UAnimationStateMachineSchema::StaticClass());
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Failed to create state machine subgraph"));
		return;
	}
	Cast<UAnimationStateMachineGraph>(SMGraph)->OwnerAnimGraphNode = SMNode;
	SMNode->EditorStateMachineGraph = Cast<UAnimationStateMachineGraph>(SMGraph);
	AnimGraph->SubGraphs.AddUnique(SMGraph);
	SMNode->AllocateDefaultPins();
	const UEdGraphSchema* Schema = SMGraph->GetSchema();
	if (Schema)
	{
		Schema->CreateDefaultNodesForGraph(*SMGraph);
	}
	Ctx.Entry->SetStringField(TEXT("GraphName"),        AnimGraph->GetName());
	Ctx.Entry->SetStringField(TEXT("StateMachineName"), SMGraph->GetName());
	Ctx.Entry->SetStringField(TEXT("AddedNodeGuid"),    SMNode->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveStateMachine(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("StateMachineName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveStateMachine requires StateMachineName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	if (!SMNode)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UEdGraph* SMGraph = SMNode->EditorStateMachineGraph;
	if (SMGraph)
	{
		AnimGraph->SubGraphs.Remove(SMGraph);
		FBlueprintEditorUtils::RemoveGraph(AnimBP, SMGraph, EGraphRemoveFlags::Recompile);
	}
	SMNode->Modify();
	DestroyGraphNode(SMNode);
	Ctx.Entry->SetStringField(TEXT("GraphName"),        AnimGraph->GetName());
	Ctx.Entry->SetStringField(TEXT("StateMachineName"), SMName);
	MarkAnimModified(Ctx);
}

static void HandleABP_AddState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("StateMachineName"));
	const FString StateName = FNexusArgs(Op).Str(TEXT("StateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddState requires StateMachineName"));
		return;
	}
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddState requires StateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	if (FNexusAnimGraphUtils::FindStateByName(SMGraph, StateName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State '%s' already exists in '%s'"), *StateName, *SMName));
		return;
	}
	UAnimStateNode* StateNode = NewObject<UAnimStateNode>(SMGraph);
	StateNode->CreateNewGuid();
	StateNode->NodePosX = PosXOf(Op);
	StateNode->NodePosY = PosYOf(Op);
	SMGraph->AddNode(StateNode, /*bFromUI*/false, /*bSelectNewNode*/false);
	StateNode->AllocateDefaultPins();
	UEdGraph* StateGraph = FBlueprintEditorUtils::CreateNewGraph(
		StateNode, FName(*StateName),
		UAnimationStateGraph::StaticClass(),
		UAnimationGraphSchema::StaticClass());
	if (StateGraph)
	{
		const UEdGraphSchema* Schema = StateGraph->GetSchema();
		if (Schema)
		{
			Schema->CreateDefaultNodesForGraph(*StateGraph);
		}
		StateNode->BoundGraph = StateGraph;
		SMGraph->SubGraphs.AddUnique(StateGraph);
	}
	Ctx.Entry->SetStringField(TEXT("StateMachineName"), SMName);
	Ctx.Entry->SetStringField(TEXT("StateName"),        StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : StateName);
	Ctx.Entry->SetStringField(TEXT("AddedNodeGuid"),    StateNode->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("StateMachineName"));
	const FString StateName = FNexusArgs(Op).Str(TEXT("StateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveState requires StateMachineName"));
		return;
	}
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveState requires StateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UAnimStateNode* StateNode = FNexusAnimGraphUtils::FindStateByName(SMGraph, StateName);
	if (!StateNode)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State '%s' not found in '%s'"), *SMName, *StateName));
		return;
	}
	TArray<UAnimStateTransitionNode*> RelatedTransitions;
	CollectTransitionsForState(SMGraph, StateNode, RelatedTransitions);
	int32 RemovedTransitions = 0;
	for (UAnimStateTransitionNode* Trans : RelatedTransitions)
	{
		if (UEdGraph* TransGraph = Trans->BoundGraph)
		{
			SMGraph->SubGraphs.Remove(TransGraph);
			FBlueprintEditorUtils::RemoveGraph(AnimBP, TransGraph, EGraphRemoveFlags::None);
		}
		DestroyGraphNode(Trans);
		++RemovedTransitions;
	}
	if (StateNode->BoundGraph)
	{
		SMGraph->SubGraphs.Remove(StateNode->BoundGraph);
		FBlueprintEditorUtils::RemoveGraph(AnimBP, StateNode->BoundGraph, EGraphRemoveFlags::None);
	}
	StateNode->Modify();
	DestroyGraphNode(StateNode);
	Ctx.Entry->SetStringField(TEXT("StateMachineName"),    SMName);
	Ctx.Entry->SetStringField(TEXT("StateName"),           StateName);
	Ctx.Entry->SetNumberField(TEXT("RemovedTransitions"),  RemovedTransitions);
	MarkAnimModified(Ctx);
}

static void HandleABP_AddTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("StateMachineName"));
	const FString SourceName = FNexusArgs(Op).Str(TEXT("StateName"));
	const FString TargetName = FNexusArgs(Op).Str(TEXT("TargetStateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTransition requires StateMachineName"));
		return;
	}
	if (SourceName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTransition requires StateName (source)"));
		return;
	}
	if (TargetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTransition requires TargetStateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UAnimStateNode* Source = FNexusAnimGraphUtils::FindStateByName(SMGraph, SourceName);
	UAnimStateNode* Target = FNexusAnimGraphUtils::FindStateByName(SMGraph, TargetName);
	if (!Source || !Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(
			TEXT("Source state '%s' or target state '%s' not found"), *SourceName, *TargetName));
		return;
	}
	if (FNexusAnimGraphUtils::FindTransition(SMGraph, SourceName, TargetName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(
			TEXT("Transition '%s' -> '%s' already exists"), *SourceName, *TargetName));
		return;
	}
	UAnimStateTransitionNode* Trans = NewObject<UAnimStateTransitionNode>(SMGraph);
	Trans->CreateNewGuid();
	Trans->NodePosX = (int32)((Source->NodePosX + Target->NodePosX) / 2);
	Trans->NodePosY = (int32)((Source->NodePosY + Target->NodePosY) / 2);
	SMGraph->AddNode(Trans, /*bFromUI*/false, /*bSelectNewNode*/false);
	Trans->AllocateDefaultPins();
	UEdGraph* RuleGraph = FBlueprintEditorUtils::CreateNewGraph(
		Trans, FName(*FString::Printf(TEXT("Trans_%s_to_%s"), *SourceName, *TargetName)),
		UAnimationTransitionGraph::StaticClass(),
		UAnimationGraphSchema::StaticClass());
	if (RuleGraph)
	{
		const UEdGraphSchema* Schema = RuleGraph->GetSchema();
		if (Schema)
		{
			Schema->CreateDefaultNodesForGraph(*RuleGraph);
		}
		Trans->BoundGraph = RuleGraph;
		SMGraph->SubGraphs.AddUnique(RuleGraph);
	}
	UEdGraphPin* SourceOut = FNexusAnimGraphUtils::GetStateOutputPin(Source);
	UEdGraphPin* TargetIn  = FNexusAnimGraphUtils::GetStateInputPin(Target);
	UEdGraphPin* TransIn   = FNexusAnimGraphUtils::GetStateInputPin(Trans);
	UEdGraphPin* TransOut  = FNexusAnimGraphUtils::GetStateOutputPin(Trans);
	if (SourceOut && TransIn) { SourceOut->MakeLinkTo(TransIn); }
	if (TransOut && TargetIn) { TransOut->MakeLinkTo(TargetIn); }
	Ctx.Entry->SetStringField(TEXT("StateMachineName"), SMName);
	Ctx.Entry->SetStringField(TEXT("StateName"),        SourceName);
	Ctx.Entry->SetStringField(TEXT("TargetStateName"),  TargetName);
	Ctx.Entry->SetStringField(TEXT("AddedNodeGuid"),    Trans->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("StateMachineName"));
	const FString SourceName = FNexusArgs(Op).Str(TEXT("StateName"));
	const FString TargetName = FNexusArgs(Op).Str(TEXT("TargetStateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTransition requires StateMachineName"));
		return;
	}
	if (SourceName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTransition requires StateName (source)"));
		return;
	}
	if (TargetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTransition requires TargetStateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UAnimStateTransitionNode* Trans = FNexusAnimGraphUtils::FindTransition(SMGraph, SourceName, TargetName);
	if (!Trans)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(
			TEXT("Transition '%s' -> '%s' not found"), *SourceName, *TargetName));
		return;
	}
	if (Trans->BoundGraph)
	{
		SMGraph->SubGraphs.Remove(Trans->BoundGraph);
		FBlueprintEditorUtils::RemoveGraph(AnimBP, Trans->BoundGraph, EGraphRemoveFlags::None);
	}
	Trans->Modify();
	DestroyGraphNode(Trans);
	Ctx.Entry->SetStringField(TEXT("StateMachineName"), SMName);
	Ctx.Entry->SetStringField(TEXT("StateName"),        SourceName);
	Ctx.Entry->SetStringField(TEXT("TargetStateName"),  TargetName);
	MarkAnimModified(Ctx);
}

static void ApplyOptionalNodeFields(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Op, TSharedPtr<FJsonObject>& Entry, bool bFailIfAssetMissing)
{
	FString SeqPath = FNexusArgs(Op).Str(TEXT("SequencePath"));
	if (SeqPath.IsEmpty()) SeqPath = FNexusArgs(Op).Str(TEXT("AssetPath"));
	if (!SeqPath.IsEmpty())
	{
		if (UAnimGraphNode_AssetPlayerBase* Player = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
		{
			UAnimationAsset* AnimAsset = FNexusAssetUtils::LoadAssetWithFallback<UAnimationAsset>(SeqPath);
			if (AnimAsset)
			{
				Player->SetAnimationAsset(AnimAsset);
			}
			else if (bFailIfAssetMissing)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Animation asset not found: %s"), *SeqPath));
			}
		}
	}
	const FString SlotName = FNexusArgs(Op).Str(TEXT("SlotName"));
	if (!SlotName.IsEmpty())
	{
		if (UAnimGraphNode_Slot* SlotNode = Cast<UAnimGraphNode_Slot>(Node))
		{
			SlotNode->Node.SlotName = FName(*SlotName);
		}
	}
	if (Op->HasField(TEXT("BoneName")))
	{
		FNexusAnimGraphUtils::ApplyBoneName(Node, FNexusArgs(Op).Str(TEXT("BoneName")));
	}
}

static void HandleABP_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString NodeClassName = FNexusArgs(Op).Str(TEXT("NodeClass"));
	UClass* NodeClass = FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(NodeClassName);
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	if (!AnimGraph)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AnimGraph not found"));
		return;
	}
	if (!NodeClass)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			TEXT("Unknown NodeClass (see schema: SequencePlayer/BlendSpacePlayer/…)"));
		return;
	}
	FString SpawnErr;
	UEdGraphNode* Node = FNexusAnimGraphUtils::SpawnAnimGraphNode(AnimGraph, NodeClass, PosXOf(Op), PosYOf(Op), SpawnErr);
	if (!Node)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), SpawnErr);
		return;
	}
	ApplyOptionalNodeFields(Node, Op, Ctx.Entry, /*bFailIfAssetMissing=*/false);
	Ctx.Entry->SetStringField(TEXT("NodeId"), Node->NodeGuid.ToString());
	Ctx.Entry->SetStringField(TEXT("NodeClass"), NodeClass->GetName());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString NodeId = FNexusArgs(Op).Str(TEXT("NodeId"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* Node = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, NodeId) : nullptr;
	if (!Node)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveNode requires existing NodeId"));
		return;
	}
	DestroyGraphNode(Node);
	Ctx.Entry->SetStringField(TEXT("NodeId"), NodeId);
	MarkAnimModified(Ctx);
}

static void HandleABP_SetNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString NodeId = FNexusArgs(Op).Str(TEXT("NodeId"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* Node = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, NodeId) : nullptr;
	if (!Node)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetNode requires existing NodeId"));
		return;
	}
	if (Op->HasField(TEXT("PosX"))) Node->NodePosX = PosXOf(Op);
	if (Op->HasField(TEXT("PosY"))) Node->NodePosY = PosYOf(Op);
	ApplyOptionalNodeFields(Node, Op, Ctx.Entry, /*bFailIfAssetMissing=*/true);
	if (Ctx.Entry->HasField(TEXT("Error")))
	{
		return;
	}
	Ctx.Entry->SetStringField(TEXT("NodeId"), Node->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_Connect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SrcId = FNexusArgs(Op).Str(TEXT("SourceNodeId"));
	const FString SrcPin = FNexusArgs(Op).Str(TEXT("SourcePinName"));
	const FString DstId = FNexusArgs(Op).Str(TEXT("TargetNodeId"));
	const FString DstPin = FNexusArgs(Op).Str(TEXT("TargetPinName"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* SrcNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, SrcId) : nullptr;
	UEdGraphNode* DstNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, DstId) : nullptr;
	if (!SrcNode || !DstNode)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("connect/disconnect requires valid SourceNodeId/TargetNodeId"));
		return;
	}
	FString ConnErr;
	if (!FNexusAnimGraphUtils::ConnectAnimPins(SrcNode, SrcPin, DstNode, DstPin, ConnErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ConnErr);
		return;
	}
	MarkAnimModified(Ctx);
}

static void HandleABP_Disconnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SrcId = FNexusArgs(Op).Str(TEXT("SourceNodeId"));
	const FString SrcPin = FNexusArgs(Op).Str(TEXT("SourcePinName"));
	const FString DstId = FNexusArgs(Op).Str(TEXT("TargetNodeId"));
	const FString DstPin = FNexusArgs(Op).Str(TEXT("TargetPinName"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* SrcNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, SrcId) : nullptr;
	UEdGraphNode* DstNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, DstId) : nullptr;
	if (!SrcNode || !DstNode)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("connect/disconnect requires valid SourceNodeId/TargetNodeId"));
		return;
	}
	UEdGraphPin* FoundSrc = nullptr;
	UEdGraphPin* FoundDst = nullptr;
	for (UEdGraphPin* P : SrcNode->Pins)
	{
		if (P && P->PinName.ToString().Equals(SrcPin, ESearchCase::IgnoreCase)) FoundSrc = P;
	}
	for (UEdGraphPin* P : DstNode->Pins)
	{
		if (P && P->PinName.ToString().Equals(DstPin, ESearchCase::IgnoreCase)) FoundDst = P;
	}
	if (FoundSrc && FoundDst)
	{
		FoundSrc->BreakLinkTo(FoundDst);
		MarkAnimModified(Ctx);
	}
	else
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("disconnect: matching pin not found"));
	}
}
#endif // WITH_EDITOR

bool FNexusManageAssetAnimBlueprintCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UAnimBlueprint* AnimBP = FNexusAssetUtils::LoadAssetWithFallback<UAnimBlueprint>(AssetPath);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath);
		return false;
	}
	FAnimBPActionState* State = new FAnimBPActionState();
	State->AnimBP = AnimBP;
	OutTarget = State;
	return true;
#else
	OutError = TEXT("ManageAssetAnimBlueprint only available in editor builds");
	return false;
#endif
}

void FNexusManageAssetAnimBlueprintCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FAnimBPActionState* State = static_cast<FAnimBPActionState*>(Target);
	if (!State)
	{
		return;
	}
	if (State->bModified && State->AnimBP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(State->AnimBP);
		FKismetEditorUtilities::CompileBlueprint(State->AnimBP);
	}
	delete State;
#else
	(void)Target;
#endif
}

void FNexusManageAssetAnimBlueprintCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("AddStateMachine"),    &HandleABP_AddStateMachine);
	OutHandlers.Add(TEXT("RemoveStateMachine"), &HandleABP_RemoveStateMachine);
	OutHandlers.Add(TEXT("AddState"),            &HandleABP_AddState);
	OutHandlers.Add(TEXT("RemoveState"),         &HandleABP_RemoveState);
	OutHandlers.Add(TEXT("AddTransition"),       &HandleABP_AddTransition);
	OutHandlers.Add(TEXT("RemoveTransition"),    &HandleABP_RemoveTransition);
	OutHandlers.Add(TEXT("AddNode"),             &HandleABP_AddNode);
	OutHandlers.Add(TEXT("RemoveNode"),          &HandleABP_RemoveNode);
	OutHandlers.Add(TEXT("SetNode"),             &HandleABP_SetNode);
	OutHandlers.Add(TEXT("Connect"),              &HandleABP_Connect);
	OutHandlers.Add(TEXT("Disconnect"),           &HandleABP_Disconnect);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetAnimBlueprintCapability)
