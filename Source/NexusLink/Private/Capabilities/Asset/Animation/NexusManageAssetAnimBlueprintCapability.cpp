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

/** 在指定 SMGraph 中查找连接 source 或 target == state 的所有 transition 节点。 */
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

void FManageAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_anim_blueprint");
	Out.SearchAssetTypes = {TEXT("AnimBlueprint")};
	Out.Description = TEXT("Batch edit ABP. State machines and AnimGraph nodes; avoid K2.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("add_state_machine"), TEXT("remove_state_machine"),
			  TEXT("add_state"),         TEXT("remove_state"),
			  TEXT("add_transition"),    TEXT("remove_transition"),
			  TEXT("add_node"), TEXT("remove_node"), TEXT("set_node"),
			  TEXT("connect"), TEXT("disconnect") }))
		.Prop(TEXT("graphName"),        FNexusSchema::Str(TEXT("Owning AnimGraph name (default AnimGraph)")))
		.Prop(TEXT("stateMachineName"), FNexusSchema::Str(TEXT("State machine name (bound graph)")))
		.Prop(TEXT("stateName"),        FNexusSchema::Str(TEXT("State name (add/remove_state, transition source)")))
		.Prop(TEXT("targetStateName"),  FNexusSchema::Str(TEXT("Transition target state name")))
		.Prop(TEXT("nodeClass"),        FNexusSchema::Enum(TEXT("AnimGraph node class (add_node)"),
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
		.Prop(TEXT("nodeId"),           FNexusSchema::Str(TEXT("node GUID (remove/set_node/connect)")))
		.Prop(TEXT("sequencePath"),     FNexusSchema::Str(TEXT("Animation asset path (set_node Sequence/BlendSpace/AimOffset)")))
		.Prop(TEXT("slotName"),         FNexusSchema::Str(TEXT("Slot name (set_node Slot)")))
		.Prop(TEXT("boneName"),         FNexusSchema::Str(TEXT("Bone name (TwoBoneIK=IKBone; FABRIK/CCDIK=TipBone; LookAt/ModifyBone; CopyBone=TargetBone)")))
		.Prop(TEXT("sourceNodeId"),     FNexusSchema::Str(TEXT("Source node GUID (connect/disconnect)")))
		.Prop(TEXT("sourcePinName"),    FNexusSchema::Str(TEXT("Source pin name")))
		.Prop(TEXT("targetNodeId"),     FNexusSchema::Str(TEXT("Target node GUID")))
		.Prop(TEXT("targetPinName"),    FNexusSchema::Str(TEXT("Target pin name")))
		.Prop(TEXT("posX"),             FNexusSchema::Num(TEXT("Editor node X (optional)")))
		.Prop(TEXT("posY"),             FNexusSchema::Num(TEXT("Editor node Y (optional)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("AnimBlueprint asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("abp"), TEXT("statemachine"), TEXT("state"), TEXT("transition"), TEXT("animgraph")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_anim_blueprint"), TEXT("create_asset_anim_blueprint"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("State machine and AnimGraph node CRUD; do not use manage_asset_blueprint for AnimGraph");
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
	return FNexusArgs(Op).Str(TEXT("graphName"));
}

static int32 PosXOf(const TSharedPtr<FJsonObject>& Op)
{
	return static_cast<int32>(FNexusArgs(Op).Num(TEXT("posX"), 0.0));
}

static int32 PosYOf(const TSharedPtr<FJsonObject>& Op)
{
	return static_cast<int32>(FNexusArgs(Op).Num(TEXT("posY"), 0.0));
}

static void HandleABP_AddStateMachine(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("stateMachineName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_state_machine requires stateMachineName"));
		return;
	}
	const FString GraphName = GraphNameOf(Op);
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
	if (!AnimGraph)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimGraph '%s' not found in AnimBlueprint"), GraphName.IsEmpty() ? TEXT("AnimGraph") : *GraphName));
		return;
	}
	if (FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' already exists"), *SMName));
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Failed to create state machine subgraph"));
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
	Ctx.Entry->SetStringField(TEXT("graphName"),        AnimGraph->GetName());
	Ctx.Entry->SetStringField(TEXT("stateMachineName"), SMGraph->GetName());
	Ctx.Entry->SetStringField(TEXT("addedNodeGuid"),    SMNode->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveStateMachine(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("stateMachineName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_state_machine requires stateMachineName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	if (!SMNode)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
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
	Ctx.Entry->SetStringField(TEXT("graphName"),        AnimGraph->GetName());
	Ctx.Entry->SetStringField(TEXT("stateMachineName"), SMName);
	MarkAnimModified(Ctx);
}

static void HandleABP_AddState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("stateMachineName"));
	const FString StateName = FNexusArgs(Op).Str(TEXT("stateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_state requires stateMachineName"));
		return;
	}
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_state requires stateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	if (FNexusAnimGraphUtils::FindStateByName(SMGraph, StateName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' already exists in '%s'"), *StateName, *SMName));
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
	Ctx.Entry->SetStringField(TEXT("stateMachineName"), SMName);
	Ctx.Entry->SetStringField(TEXT("stateName"),        StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : StateName);
	Ctx.Entry->SetStringField(TEXT("addedNodeGuid"),    StateNode->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("stateMachineName"));
	const FString StateName = FNexusArgs(Op).Str(TEXT("stateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_state requires stateMachineName"));
		return;
	}
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_state requires stateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UAnimStateNode* StateNode = FNexusAnimGraphUtils::FindStateByName(SMGraph, StateName);
	if (!StateNode)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' not found in '%s'"), *SMName, *StateName));
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
	Ctx.Entry->SetStringField(TEXT("stateMachineName"),    SMName);
	Ctx.Entry->SetStringField(TEXT("stateName"),           StateName);
	Ctx.Entry->SetNumberField(TEXT("removedTransitions"),  RemovedTransitions);
	MarkAnimModified(Ctx);
}

static void HandleABP_AddTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("stateMachineName"));
	const FString SourceName = FNexusArgs(Op).Str(TEXT("stateName"));
	const FString TargetName = FNexusArgs(Op).Str(TEXT("targetStateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_transition requires stateMachineName"));
		return;
	}
	if (SourceName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_transition requires stateName (source)"));
		return;
	}
	if (TargetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_transition requires targetStateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UAnimStateNode* Source = FNexusAnimGraphUtils::FindStateByName(SMGraph, SourceName);
	UAnimStateNode* Target = FNexusAnimGraphUtils::FindStateByName(SMGraph, TargetName);
	if (!Source || !Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Source state '%s' or target state '%s' not found"), *SourceName, *TargetName));
		return;
	}
	if (FNexusAnimGraphUtils::FindTransition(SMGraph, SourceName, TargetName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
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
	Ctx.Entry->SetStringField(TEXT("stateMachineName"), SMName);
	Ctx.Entry->SetStringField(TEXT("stateName"),        SourceName);
	Ctx.Entry->SetStringField(TEXT("targetStateName"),  TargetName);
	Ctx.Entry->SetStringField(TEXT("addedNodeGuid"),    Trans->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SMName = FNexusArgs(Op).Str(TEXT("stateMachineName"));
	const FString SourceName = FNexusArgs(Op).Str(TEXT("stateName"));
	const FString TargetName = FNexusArgs(Op).Str(TEXT("targetStateName"));
	if (SMName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_transition requires stateMachineName"));
		return;
	}
	if (SourceName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_transition requires stateName (source)"));
		return;
	}
	if (TargetName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_transition requires targetStateName"));
		return;
	}
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
		? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
	UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
	if (!SMGraph)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		return;
	}
	UAnimStateTransitionNode* Trans = FNexusAnimGraphUtils::FindTransition(SMGraph, SourceName, TargetName);
	if (!Trans)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
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
	Ctx.Entry->SetStringField(TEXT("stateMachineName"), SMName);
	Ctx.Entry->SetStringField(TEXT("stateName"),        SourceName);
	Ctx.Entry->SetStringField(TEXT("targetStateName"),  TargetName);
	MarkAnimModified(Ctx);
}

static void ApplyOptionalNodeFields(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Op, TSharedPtr<FJsonObject>& Entry, bool bFailIfAssetMissing)
{
	FString SeqPath = FNexusArgs(Op).Str(TEXT("sequencePath"));
	if (SeqPath.IsEmpty()) SeqPath = FNexusArgs(Op).Str(TEXT("assetPath"));
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
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Animation asset not found: %s"), *SeqPath));
			}
		}
	}
	const FString SlotName = FNexusArgs(Op).Str(TEXT("slotName"));
	if (!SlotName.IsEmpty())
	{
		if (UAnimGraphNode_Slot* SlotNode = Cast<UAnimGraphNode_Slot>(Node))
		{
			SlotNode->Node.SlotName = FName(*SlotName);
		}
	}
	if (Op->HasField(TEXT("boneName")))
	{
		FNexusAnimGraphUtils::ApplyBoneName(Node, FNexusArgs(Op).Str(TEXT("boneName")));
	}
}

static void HandleABP_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString NodeClassName = FNexusArgs(Op).Str(TEXT("nodeClass"));
	UClass* NodeClass = FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(NodeClassName);
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	if (!AnimGraph)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("AnimGraph not found"));
		return;
	}
	if (!NodeClass)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			TEXT("Unknown nodeClass (see schema: SequencePlayer/BlendSpacePlayer/…)"));
		return;
	}
	FString SpawnErr;
	UEdGraphNode* Node = FNexusAnimGraphUtils::SpawnAnimGraphNode(AnimGraph, NodeClass, PosXOf(Op), PosYOf(Op), SpawnErr);
	if (!Node)
	{
		Ctx.Entry->SetStringField(TEXT("error"), SpawnErr);
		return;
	}
	ApplyOptionalNodeFields(Node, Op, Ctx.Entry, /*bFailIfAssetMissing=*/false);
	Ctx.Entry->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	Ctx.Entry->SetStringField(TEXT("nodeClass"), NodeClass->GetName());
	MarkAnimModified(Ctx);
}

static void HandleABP_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString NodeId = FNexusArgs(Op).Str(TEXT("nodeId"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* Node = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, NodeId) : nullptr;
	if (!Node)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_node requires existing nodeId"));
		return;
	}
	DestroyGraphNode(Node);
	Ctx.Entry->SetStringField(TEXT("nodeId"), NodeId);
	MarkAnimModified(Ctx);
}

static void HandleABP_SetNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString NodeId = FNexusArgs(Op).Str(TEXT("nodeId"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* Node = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, NodeId) : nullptr;
	if (!Node)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_node requires existing nodeId"));
		return;
	}
	if (Op->HasField(TEXT("posX"))) Node->NodePosX = PosXOf(Op);
	if (Op->HasField(TEXT("posY"))) Node->NodePosY = PosYOf(Op);
	ApplyOptionalNodeFields(Node, Op, Ctx.Entry, /*bFailIfAssetMissing=*/true);
	if (Ctx.Entry->HasField(TEXT("error")))
	{
		return;
	}
	Ctx.Entry->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	MarkAnimModified(Ctx);
}

static void HandleABP_Connect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SrcId = FNexusArgs(Op).Str(TEXT("sourceNodeId"));
	const FString SrcPin = FNexusArgs(Op).Str(TEXT("sourcePinName"));
	const FString DstId = FNexusArgs(Op).Str(TEXT("targetNodeId"));
	const FString DstPin = FNexusArgs(Op).Str(TEXT("targetPinName"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* SrcNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, SrcId) : nullptr;
	UEdGraphNode* DstNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, DstId) : nullptr;
	if (!SrcNode || !DstNode)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("connect/disconnect requires valid sourceNodeId/targetNodeId"));
		return;
	}
	FString ConnErr;
	if (!FNexusAnimGraphUtils::ConnectAnimPins(SrcNode, SrcPin, DstNode, DstPin, ConnErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), ConnErr);
		return;
	}
	MarkAnimModified(Ctx);
}

static void HandleABP_Disconnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UAnimBlueprint* AnimBP = AnimBPFrom(Ctx);
	const FString SrcId = FNexusArgs(Op).Str(TEXT("sourceNodeId"));
	const FString SrcPin = FNexusArgs(Op).Str(TEXT("sourcePinName"));
	const FString DstId = FNexusArgs(Op).Str(TEXT("targetNodeId"));
	const FString DstPin = FNexusArgs(Op).Str(TEXT("targetPinName"));
	UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphNameOf(Op));
	UEdGraphNode* SrcNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, SrcId) : nullptr;
	UEdGraphNode* DstNode = AnimGraph ? FNexusAnimGraphUtils::FindNodeByGuidOrTitle(AnimGraph, DstId) : nullptr;
	if (!SrcNode || !DstNode)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("connect/disconnect requires valid sourceNodeId/targetNodeId"));
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("disconnect: matching pin not found"));
	}
}
#endif // WITH_EDITOR

bool FManageAssetAnimBlueprintCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
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
	OutError = TEXT("manage_asset_anim_blueprint only available in editor builds");
	return false;
#endif
}

void FManageAssetAnimBlueprintCapability::FinalizeTarget(void* Target) const
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

void FManageAssetAnimBlueprintCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("add_state_machine"),    &HandleABP_AddStateMachine);
	OutHandlers.Add(TEXT("remove_state_machine"), &HandleABP_RemoveStateMachine);
	OutHandlers.Add(TEXT("add_state"),            &HandleABP_AddState);
	OutHandlers.Add(TEXT("remove_state"),         &HandleABP_RemoveState);
	OutHandlers.Add(TEXT("add_transition"),       &HandleABP_AddTransition);
	OutHandlers.Add(TEXT("remove_transition"),    &HandleABP_RemoveTransition);
	OutHandlers.Add(TEXT("add_node"),             &HandleABP_AddNode);
	OutHandlers.Add(TEXT("remove_node"),          &HandleABP_RemoveNode);
	OutHandlers.Add(TEXT("set_node"),             &HandleABP_SetNode);
	OutHandlers.Add(TEXT("connect"),              &HandleABP_Connect);
	OutHandlers.Add(TEXT("disconnect"),           &HandleABP_Disconnect);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimBlueprintCapability)
