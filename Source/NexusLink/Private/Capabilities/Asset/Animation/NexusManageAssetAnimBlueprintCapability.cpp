// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetAnimBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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

// ─── Execute ─────────────────────────────────────────────────────────────────

void FManageAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_anim_blueprint");
	Out.Description = TEXT("编辑 ABP 状态机。增删 state_machine/state/transition；须保存。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),        FNexusSchema::Str(TEXT("动画蓝图资产路径")))
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("操作类型"),
			{ TEXT("add_state_machine"), TEXT("remove_state_machine"),
			  TEXT("add_state"),         TEXT("remove_state"),
			  TEXT("add_transition"),    TEXT("remove_transition") }))
		.Prop(TEXT("graphName"),        FNexusSchema::Str(TEXT("所属 AnimGraph 名（默认 AnimGraph）")))
		.Prop(TEXT("stateMachineName"), FNexusSchema::Str(TEXT("状态机名（boundgraph 名）")))
		.Prop(TEXT("stateName"),        FNexusSchema::Str(TEXT("状态名（add/remove_state、过渡源）")))
		.Prop(TEXT("targetStateName"),  FNexusSchema::Str(TEXT("过渡目标状态名")))
		.Prop(TEXT("posX"),             FNexusSchema::Num(TEXT("编辑器节点 X 坐标（可选）")))
		.Prop(TEXT("posY"),             FNexusSchema::Num(TEXT("编辑器节点 Y 坐标（可选）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("abp"), TEXT("statemachine"), TEXT("state"), TEXT("transition"), TEXT("animgraph")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_anim_blueprint"), TEXT("create_asset_anim_blueprint"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("状态机 CRUD；AnimGraph 用 manage_asset_blueprint");
}

FCapabilityResult FManageAssetAnimBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		FString Action;
		if (!Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}
		Action.ToLowerInline();

		UAnimBlueprint* AnimBP = FNexusAssetUtils::LoadAssetWithFallback<UAnimBlueprint>(AssetPath);
		if (!AnimBP)
		{
			OutError = FString::Printf(TEXT("AnimBlueprint 未找到: %s"), *AssetPath);
			return;
		}

		Entry->SetStringField(TEXT("action"), Action);

		FString GraphName;
		Arguments->TryGetStringField(TEXT("graphName"), GraphName);

		const float PosX = Arguments->HasField(TEXT("posX")) ? (float)Arguments->GetNumberField(TEXT("posX")) : 0.0f;
		const float PosY = Arguments->HasField(TEXT("posY")) ? (float)Arguments->GetNumberField(TEXT("posY")) : 0.0f;

		bool bModified = false;

		// ── add_state_machine ──────────────────────────────────────────────────────
		if (Action == TEXT("add_state_machine"))
		{
			FString SMName;
			if (!Arguments->TryGetStringField(TEXT("stateMachineName"), SMName) || SMName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_state_machine 需要 stateMachineName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
			if (!AnimGraph)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint 中未找到 AnimGraph '%s'"), GraphName.IsEmpty() ? TEXT("AnimGraph") : *GraphName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 查重：同名状态机节点不能重复
			if (FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' already exists"), *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 1. 创建状态机节点
			UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
			SMNode->CreateNewGuid();
			SMNode->NodePosX = (int32)PosX;
			SMNode->NodePosY = (int32)PosY;
			AnimGraph->AddNode(SMNode, /*bFromUI*/false, /*bSelectNewNode*/false);

			// 2. 创建子图（状态机内部图）
			UEdGraph* SMGraph = FBlueprintEditorUtils::CreateNewGraph(
				SMNode, FName(*SMName),
				UAnimationStateMachineGraph::StaticClass(),
				UAnimationStateMachineSchema::StaticClass());
			if (!SMGraph)
			{
				Entry->SetStringField(TEXT("error"), TEXT("创建状态机子图失败"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 3. 绑定子图 + 加入父图 SubGraphs
			Cast<UAnimationStateMachineGraph>(SMGraph)->OwnerAnimGraphNode = SMNode;
			SMNode->EditorStateMachineGraph = Cast<UAnimationStateMachineGraph>(SMGraph);
			AnimGraph->SubGraphs.AddUnique(SMGraph);

			// 4. AllocateDefaultPins + 让 Schema 创建默认 entry node
			SMNode->AllocateDefaultPins();
			const UEdGraphSchema* Schema = SMGraph->GetSchema();
			if (Schema)
			{
				Schema->CreateDefaultNodesForGraph(*SMGraph);
			}

			Entry->SetStringField(TEXT("graphName"),        AnimGraph->GetName());
			Entry->SetStringField(TEXT("stateMachineName"), SMGraph->GetName());
			Entry->SetStringField(TEXT("addedNodeGuid"),    SMNode->NodeGuid.ToString());
			bModified = true;
		}
		// ── remove_state_machine ───────────────────────────────────────────────────
		else if (Action == TEXT("remove_state_machine"))
		{
			FString SMName;
			if (!Arguments->TryGetStringField(TEXT("stateMachineName"), SMName) || SMName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_state_machine 需要 stateMachineName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
			UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
				? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
			if (!SMNode)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("状态机 '%s' 未找到"), *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* SMGraph = SMNode->EditorStateMachineGraph;
			// 1. 删子图（同时移除 AnimGraph->SubGraphs）
			if (SMGraph)
			{
				AnimGraph->SubGraphs.Remove(SMGraph);
				FBlueprintEditorUtils::RemoveGraph(AnimBP, SMGraph, EGraphRemoveFlags::Recompile);
			}
			// 2. 删节点
			SMNode->Modify();
			DestroyGraphNode(SMNode);

			Entry->SetStringField(TEXT("graphName"),        AnimGraph->GetName());
			Entry->SetStringField(TEXT("stateMachineName"), SMName);
			bModified = true;
		}
		// ── add_state ──────────────────────────────────────────────────────────────
		else if (Action == TEXT("add_state"))
		{
			FString SMName, StateName;
			if (!Arguments->TryGetStringField(TEXT("stateMachineName"), SMName) || SMName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_state 需要 stateMachineName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!Arguments->TryGetStringField(TEXT("stateName"), StateName) || StateName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_state 需要 stateName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
			UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
				? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
			UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
			if (!SMGraph)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("状态机 '%s' 未找到"), *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			if (FNexusAnimGraphUtils::FindStateByName(SMGraph, StateName))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' already exists in '%s'"), *StateName, *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 1. 创建 state 节点
			UAnimStateNode* StateNode = NewObject<UAnimStateNode>(SMGraph);
			StateNode->CreateNewGuid();
			StateNode->NodePosX = (int32)PosX;
			StateNode->NodePosY = (int32)PosY;
			SMGraph->AddNode(StateNode, /*bFromUI*/false, /*bSelectNewNode*/false);
			StateNode->AllocateDefaultPins();

			// 2. 为 state 创建 BoundGraph（内部 anim 图）
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

			Entry->SetStringField(TEXT("stateMachineName"), SMName);
			Entry->SetStringField(TEXT("stateName"),        StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : StateName);
			Entry->SetStringField(TEXT("addedNodeGuid"),    StateNode->NodeGuid.ToString());
			bModified = true;
		}
		// ── remove_state ───────────────────────────────────────────────────────────
		else if (Action == TEXT("remove_state"))
		{
			FString SMName, StateName;
			if (!Arguments->TryGetStringField(TEXT("stateMachineName"), SMName) || SMName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_state 需要 stateMachineName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!Arguments->TryGetStringField(TEXT("stateName"), StateName) || StateName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_state 需要 stateName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
			UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
				? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
			UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
			if (!SMGraph)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("状态机 '%s' 未找到"), *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UAnimStateNode* StateNode = FNexusAnimGraphUtils::FindStateByName(SMGraph, StateName);
			if (!StateNode)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("在 '%s' 中未找到状态 '%s'"), *SMName, *StateName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 1. 级联删除连接此 state 的所有 transition
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

			// 2. 删 state BoundGraph
			if (StateNode->BoundGraph)
			{
				SMGraph->SubGraphs.Remove(StateNode->BoundGraph);
				FBlueprintEditorUtils::RemoveGraph(AnimBP, StateNode->BoundGraph, EGraphRemoveFlags::None);
			}

			// 3. 删节点
			StateNode->Modify();
			DestroyGraphNode(StateNode);

			Entry->SetStringField(TEXT("stateMachineName"),    SMName);
			Entry->SetStringField(TEXT("stateName"),           StateName);
			Entry->SetNumberField(TEXT("removedTransitions"),  RemovedTransitions);
			bModified = true;
		}
		// ── add_transition ─────────────────────────────────────────────────────────
		else if (Action == TEXT("add_transition"))
		{
			FString SMName, SourceName, TargetName;
			if (!Arguments->TryGetStringField(TEXT("stateMachineName"), SMName) || SMName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_transition 需要 stateMachineName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!Arguments->TryGetStringField(TEXT("stateName"), SourceName) || SourceName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_transition 需要 stateName（源）"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!Arguments->TryGetStringField(TEXT("targetStateName"), TargetName) || TargetName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_transition 需要 targetStateName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
			UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
				? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
			UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
			if (!SMGraph)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("状态机 '%s' 未找到"), *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UAnimStateNode* Source = FNexusAnimGraphUtils::FindStateByName(SMGraph, SourceName);
			UAnimStateNode* Target = FNexusAnimGraphUtils::FindStateByName(SMGraph, TargetName);
			if (!Source || !Target)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("源状态 '%s' 或目标状态 '%s' 未找到"), *SourceName, *TargetName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			if (FNexusAnimGraphUtils::FindTransition(SMGraph, SourceName, TargetName))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Transition '%s' -> '%s' already exists"), *SourceName, *TargetName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 1. 创建 transition 节点
			UAnimStateTransitionNode* Trans = NewObject<UAnimStateTransitionNode>(SMGraph);
			Trans->CreateNewGuid();
			Trans->NodePosX = (int32)((Source->NodePosX + Target->NodePosX) / 2);
			Trans->NodePosY = (int32)((Source->NodePosY + Target->NodePosY) / 2);
			SMGraph->AddNode(Trans, /*bFromUI*/false, /*bSelectNewNode*/false);
			Trans->AllocateDefaultPins();

			// 2. 创建 transition 规则图
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

			// 3. Pin 连接：source.output -> trans.input；trans.output -> target.input
			UEdGraphPin* SourceOut = FNexusAnimGraphUtils::GetStateOutputPin(Source);
			UEdGraphPin* TargetIn  = FNexusAnimGraphUtils::GetStateInputPin(Target);
			UEdGraphPin* TransIn   = FNexusAnimGraphUtils::GetStateInputPin(Trans);
			UEdGraphPin* TransOut  = FNexusAnimGraphUtils::GetStateOutputPin(Trans);
			if (SourceOut && TransIn) { SourceOut->MakeLinkTo(TransIn); }
			if (TransOut && TargetIn) { TransOut->MakeLinkTo(TargetIn); }

			Entry->SetStringField(TEXT("stateMachineName"), SMName);
			Entry->SetStringField(TEXT("stateName"),        SourceName);
			Entry->SetStringField(TEXT("targetStateName"),  TargetName);
			Entry->SetStringField(TEXT("addedNodeGuid"),    Trans->NodeGuid.ToString());
			bModified = true;
		}
		// ── remove_transition ──────────────────────────────────────────────────────
		else if (Action == TEXT("remove_transition"))
		{
			FString SMName, SourceName, TargetName;
			if (!Arguments->TryGetStringField(TEXT("stateMachineName"), SMName) || SMName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_transition 需要 stateMachineName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!Arguments->TryGetStringField(TEXT("stateName"), SourceName) || SourceName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_transition 需要 stateName（源）"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!Arguments->TryGetStringField(TEXT("targetStateName"), TargetName) || TargetName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_transition 需要 targetStateName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UEdGraph* AnimGraph = FNexusAnimGraphUtils::FindAnimGraph(AnimBP, GraphName);
			UAnimGraphNode_StateMachineBase* SMNode = AnimGraph
				? FNexusAnimGraphUtils::FindStateMachineNode(AnimGraph, SMName) : nullptr;
			UAnimationStateMachineGraph* SMGraph = FNexusAnimGraphUtils::GetStateMachineGraph(SMNode);
			if (!SMGraph)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("状态机 '%s' 未找到"), *SMName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UAnimStateTransitionNode* Trans = FNexusAnimGraphUtils::FindTransition(SMGraph, SourceName, TargetName);
			if (!Trans)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("过渡 '%s' -> '%s' 未找到"), *SourceName, *TargetName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 1. 删规则图
			if (Trans->BoundGraph)
			{
				SMGraph->SubGraphs.Remove(Trans->BoundGraph);
				FBlueprintEditorUtils::RemoveGraph(AnimBP, Trans->BoundGraph, EGraphRemoveFlags::None);
			}
			// 2. 删节点（含 Pin 断开）
			Trans->Modify();
			DestroyGraphNode(Trans);

			Entry->SetStringField(TEXT("stateMachineName"), SMName);
			Entry->SetStringField(TEXT("stateName"),        SourceName);
			Entry->SetStringField(TEXT("targetStateName"),  TargetName);
			bModified = true;
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
		}

		if (bModified)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
			FKismetEditorUtilities::CompileBlueprint(AnimBP);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("manage_asset_anim_blueprint 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetAnimBlueprintCapability)
