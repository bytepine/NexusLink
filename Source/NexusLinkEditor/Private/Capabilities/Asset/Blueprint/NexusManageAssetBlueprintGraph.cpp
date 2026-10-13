// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintCapability.h"
#include "NexusActionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPinTypeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "Utils/NexusEditorTransaction.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif
#include "UObject/UObjectIterator.h"
#include "NexusMcpTool.h"

#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintGraph.h"

void FNexusBlueprintGraphOps::HandleGraphNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	// ── Graph / Wire actions: require graphName ───────────────────────────────
	const FString GraphName = FNexusArgs(Op).Str(TEXT("graphName"));
	if (GraphName.IsEmpty())
	{
		Entry->SetStringField(TEXT("error"), TEXT("Node/wire ops need graphName. Hint: get_asset_blueprint(sections=[\"graphOverview\"]) first."));
	return;
	}

	UEdGraph* Graph = FNexusBlueprintGraphUtils::FindBPGraph(BP, GraphName);
	if (!Graph)
	{
		TArray<UEdGraph*> AllGraphs;
		FNexusBlueprintGraphUtils::CollectAllGraphs(BP, AllGraphs);
		TArray<FString> GraphNames;
		for (UEdGraph* G : AllGraphs)
		{
			if (G) GraphNames.Add(G->GetName());
		}
		const int32 MaxList = 12;
		if (GraphNames.Num() > MaxList)
		{
			GraphNames.SetNum(MaxList);
			GraphNames.Add(FString::Printf(TEXT("...+%d more"), AllGraphs.Num() - MaxList));
		}
		Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Graph '%s' not found. graphName is graph object name, not function. Available: %s"),
			*GraphName, *FString::Join(GraphNames, TEXT(", "))));
	return;
	}

	Entry->SetStringField(TEXT("graphName"), GraphName);
	BP->Modify();
	Graph->Modify();

	// ── Node actions ──────────────────────────────────────────────────────────
	if (Action == TEXT("add_node"))
	{
		const FString NodeClass = FNexusArgs(Op).Str(TEXT("nodeClass"));
		if (NodeClass.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_node requires nodeClass"));
	return; }
		const int32 PosX = Op->HasField(TEXT("posX")) ? static_cast<int32>(Op->GetNumberField(TEXT("posX"))) : 0;
		const int32 PosY = Op->HasField(TEXT("posY")) ? static_cast<int32>(Op->GetNumberField(TEXT("posY"))) : 0;

		UEdGraphNode* NewNode = nullptr;

		if (NodeClass == TEXT("K2Node_CallFunction"))
		{
			const FString FuncName = FNexusArgs(Op).Str(TEXT("functionName"));
			if (FuncName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("K2Node_CallFunction requires functionName"));
	return; }
			FString FuncClassName;
			Op->TryGetStringField(TEXT("functionClass"), FuncClassName);

			UFunction* Func = FNexusBlueprintGraphUtils::ResolveGraphFunction(FuncName, FuncClassName);
			if (!Func) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Function '%s' not found (tried 'K2_%s')"), *FuncName, *FuncName));
	return; }

			NewNode = FNexusBlueprintGraphUtils::MakeCallFunctionNode(Graph, Func, PosX, PosY);
		}
		else if (NodeClass == TEXT("K2Node_Event"))
		{
			// functionName：ReceiveBeginPlay / BeginPlay；functionClass 默认 Actor
			FString EventName = FNexusArgs(Op).Str(TEXT("functionName"));
			if (EventName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("K2Node_Event requires functionName (e.g. ReceiveBeginPlay)"));
	return; }
			if (EventName == TEXT("BeginPlay")) EventName = TEXT("ReceiveBeginPlay");
			FString EventClassName = TEXT("Actor");
			Op->TryGetStringField(TEXT("functionClass"), EventClassName);
			UClass* EventClass = FNexusAssetUtils::FindClassWithUPrefix(EventClassName);
			if (!EventClass) EventClass = AActor::StaticClass();
			NewNode = FNexusBlueprintGraphUtils::MakeEventNode(BP, Graph, EventName, EventClass, PosX, PosY);
		}
		else if (NodeClass == TEXT("K2Node_VariableGet") || NodeClass == TEXT("K2Node_VariableSet"))
		{
			const FString VarName = FNexusArgs(Op).Str(TEXT("variableName"));
			if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("%s requires variableName"), *NodeClass));
	return; }

			NewNode = FNexusBlueprintGraphUtils::MakeVariableNode(Graph, VarName, NodeClass == TEXT("K2Node_VariableSet"), PosX, PosY);
		}
		else
		{
			UClass* NodeUClass = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!NodeUClass || !NodeUClass->IsChildOf(UEdGraphNode::StaticClass()))
			{ Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Node class '%s' not found"), *NodeClass));
	return; }
			NewNode = FNexusBlueprintGraphUtils::MakeGenericNode(Graph, NodeUClass, PosX, PosY);
		}

	Entry->SetStringField(TEXT("nodeId"),    NewNode->NodeGuid.ToString());
	Entry->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
	Entry->SetStringField(TEXT("nodeTitle"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}

	if (Action == TEXT("remove_node"))
	{
		const FString NodeIdStr = FNexusArgs(Op).Str(TEXT("nodeId"));
		if (NodeIdStr.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId"));
	return; }
		UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
		if (!Node) { Entry->SetStringField(TEXT("error"), TEXT("Node not found"));
	return; }
		Entry->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		Node->Modify();
		Graph->GetSchema()->BreakNodeLinks(*Node);
		Graph->RemoveNode(Node);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return;
	}

	if (Action == TEXT("set_node"))
	{
		const FString NodeIdStr = FNexusArgs(Op).Str(TEXT("nodeId"));
		if (NodeIdStr.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set_node requires nodeId"));
	return; }
		UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
		if (!Node) { Entry->SetStringField(TEXT("error"), TEXT("Node not found"));
	return; }

		Node->Modify();
		if (Op->HasField(TEXT("posX")))    Node->NodePosX = static_cast<int32>(Op->GetNumberField(TEXT("posX")));
		if (Op->HasField(TEXT("posY")))    Node->NodePosY = static_cast<int32>(Op->GetNumberField(TEXT("posY")));
		if (Op->HasField(TEXT("comment"))) Node->NodeComment = Op->GetStringField(TEXT("comment"));

		if (Op->HasField(TEXT("pinName")) && Op->HasField(TEXT("pinDefaultValue")))
		{
			const FString PinName = Op->GetStringField(TEXT("pinName"));
			const FString PinVal  = Op->GetStringField(TEXT("pinDefaultValue"));
			bool bFound = false;
			for (UEdGraphPin* Pin : Node->Pins)
			{ if (Pin && Pin->PinName.ToString() == PinName) { Pin->DefaultValue = PinVal; bFound = true; break; } }
			if (!bFound) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Pin '%s' not found"), *PinName));
	return; }
		}

	Entry->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return;
	}

	// ── Promote pin → variable（对齐编辑器 DoPromoteToVariable）──────────────
	if (Action == TEXT("promote_pin"))
	{
		FString NodeIdStr, PinNameStr;
		Op->TryGetStringField(TEXT("nodeId"), NodeIdStr);
		Op->TryGetStringField(TEXT("pinName"), PinNameStr);
		if (NodeIdStr.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("promote_pin requires nodeId"));
	return; }
		if (PinNameStr.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("promote_pin requires pinName"));
	return; }

		UEdGraphNode* PinNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
		if (!PinNode) { Entry->SetStringField(TEXT("error"), TEXT("Node not found"));
	return; }
		UEdGraphPin* TargetPin = FNexusBlueprintGraphUtils::FindBPPin(PinNode, PinNameStr);
		if (!TargetPin)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Pin '%s' not found. Available pinName: %s"),
				*PinNameStr, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(PinNode)));
	return;
		}
		if (TargetPin->bOrphanedPin)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Cannot promote orphaned pin"));
	return;
		}

		bool bIsLocal = false;
		if (Op->HasField(TEXT("isLocal"))) bIsLocal = Op->GetBoolField(TEXT("isLocal"));
		if (bIsLocal && !FBlueprintEditorUtils::DoesSupportLocalVariables(Graph))
		{
			Entry->SetStringField(TEXT("error"), TEXT("Local vars unsupported on this graph (function graphs only with isLocal=true)"));
	return;
		}

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		// 与编辑器一致：exec 不可提升；其余交给 Schema（双参 API 在各 UE 版本可用）
		if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			Entry->SetStringField(TEXT("error"), TEXT("exec pin cannot be promoted to variable"));
	return;
		}
		if (!K2Schema->CanPromotePinToVariable(*TargetPin, !bIsLocal))
		{
			Entry->SetStringField(TEXT("error"), TEXT("This pin cannot be promoted to variable"));
	return;
		}

		// 提升依赖 SkeletonGeneratedClass
		if (!BP->SkeletonGeneratedClass)
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
			if (!BP->SkeletonGeneratedClass)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Blueprint has no SkeletonGeneratedClass; cannot promote_pin"));
	return;
			}
		}

		const FName SavedPinName = TargetPin->PinName;
		FEdGraphPinType NewPinType = TargetPin->PinType;
		NewPinType.bIsConst = false;
		NewPinType.bIsReference = false;
		NewPinType.bIsWeakPointer = false;
		const FString PinDefault = TargetPin->GetDefaultAsString();

		FString RequestedName;
		Op->TryGetStringField(TEXT("variableName"), RequestedName);
		const FString BaseName = !RequestedName.IsEmpty()
			? RequestedName
			: (bIsLocal ? TEXT("NewLocalVar") : TEXT("NewVar"));
		FName VarName = FBlueprintEditorUtils::FindUniqueKismetName(BP, BaseName);

		UEdGraph* FunctionGraph = nullptr;
		bool bWasSuccessful = false;
		if (!bIsLocal)
		{
			bWasSuccessful = FBlueprintEditorUtils::AddMemberVariable(BP, VarName, NewPinType, PinDefault);
		}
		else
		{
			FunctionGraph = FBlueprintEditorUtils::GetTopLevelGraph(Graph);
			if (!FunctionGraph)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Unable to resolve function graph for local variable"));
	return;
			}
			bWasSuccessful = FBlueprintEditorUtils::AddLocalVariable(BP, FunctionGraph, VarName, NewPinType, PinDefault);
		}
		if (!bWasSuccessful)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create variable: %s"), *VarName.ToString()));
	return;
		}

		// 加变量可能重建节点，刷新引脚引用
		TargetPin = PinNode->FindPin(SavedPinName);
		if (!TargetPin)
		{
			PinNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
			TargetPin = PinNode ? FNexusBlueprintGraphUtils::FindBPPin(PinNode, SavedPinName.ToString()) : nullptr;
		}
		if (!TargetPin || !PinNode)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Target pin lost after creating variable (node may have rebuilt)"));
	return;
		}

		FEdGraphSchemaAction_K2NewNode NodeInfo;
		UK2Node_Variable* TemplateNode = nullptr;
		if (TargetPin->Direction == EGPD_Input)
		{
			TemplateNode = NewObject<UK2Node_VariableGet>();
		}
		else
		{
			TemplateNode = NewObject<UK2Node_VariableSet>();
		}

		if (!bIsLocal)
		{
			TemplateNode->VariableReference.SetSelfMember(VarName);
		}
		else
		{
			TemplateNode->VariableReference.SetLocalMember(
				VarName,
				FunctionGraph->GetName(),
				FBlueprintEditorUtils::FindLocalVariableGuidByName(BP, FunctionGraph, VarName));
		}
		NodeInfo.NodeTemplate = TemplateNode;

		FVector2D NewNodePos;
		NewNodePos.X = (TargetPin->Direction == EGPD_Input)
			? static_cast<float>(PinNode->NodePosX - 200)
			: static_cast<float>(PinNode->NodePosX + 400);
		NewNodePos.Y = static_cast<float>(PinNode->NodePosY);
		if (Op->HasField(TEXT("posX"))) NewNodePos.X = static_cast<float>(Op->GetNumberField(TEXT("posX")));
		if (Op->HasField(TEXT("posY"))) NewNodePos.Y = static_cast<float>(Op->GetNumberField(TEXT("posY")));

#if NX_UE_HAS_EDGRAPH_PERFORM_ACTION_VECTOR2F
		UEdGraphNode* Spawned = NodeInfo.PerformAction(
			Graph, TargetPin,
			FVector2f(static_cast<float>(NewNodePos.X), static_cast<float>(NewNodePos.Y)),
			false);
#else
		UEdGraphNode* Spawned = NodeInfo.PerformAction(Graph, TargetPin, NewNodePos, false);
#endif
		if (!Spawned)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Create Get/Set nodefailed"));
	return;
		}

		Entry->SetStringField(TEXT("variableName"), VarName.ToString());
		Entry->SetStringField(TEXT("pinName"), SavedPinName.ToString());
		Entry->SetStringField(TEXT("nodeId"), Spawned->NodeGuid.ToString());
		Entry->SetStringField(TEXT("nodeClass"), Spawned->GetClass()->GetName());
		Entry->SetBoolField(TEXT("isLocal"), bIsLocal);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}

void FNexusBlueprintGraphOps::HandleWire(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;

	const FString GraphName = FNexusArgs(Op).Str(TEXT("graphName"));
	if (GraphName.IsEmpty())
	{
		Entry->SetStringField(TEXT("error"), TEXT("Node/wire ops need graphName. Hint: get_asset_blueprint(sections=[\"graphOverview\"]) first."));
		return;
	}
	UEdGraph* Graph = FNexusBlueprintGraphUtils::FindBPGraph(BP, GraphName);
	if (!Graph)
	{
		Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
		return;
	}
	Entry->SetStringField(TEXT("graphName"), GraphName);
	BP->Modify();
	Graph->Modify();

	const FString SrcNodeId  = FNexusArgs(Op).Str(TEXT("sourceNodeId"));
	const FString SrcPinName = FNexusArgs(Op).Str(TEXT("sourcePinName"));
	if (SrcNodeId.IsEmpty() || SrcPinName.IsEmpty())
	{ Entry->SetStringField(TEXT("error"), TEXT("Wire ops require sourceNodeId and sourcePinName"));
	return; }

	UEdGraphNode* SrcNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, SrcNodeId);
	if (!SrcNode) { Entry->SetStringField(TEXT("error"), TEXT("Source node not found"));
	return;}
	UEdGraphPin* SrcPin = FNexusBlueprintGraphUtils::FindBPPin(SrcNode, SrcPinName);
	if (!SrcPin)
	{
		Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Source pin '%s' not found. Available pinName: %s"),
			*SrcPinName, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(SrcNode)));
	return;
	}

	Entry->SetStringField(TEXT("sourceNodeId"),  SrcNodeId);
	Entry->SetStringField(TEXT("sourcePinName"), SrcPinName);

	const UEdGraphSchema* Schema = Graph->GetSchema();

	if (Action == TEXT("connect"))
	{
		const FString DstNodeId  = FNexusArgs(Op).Str(TEXT("targetNodeId"));
		const FString DstPinName = FNexusArgs(Op).Str(TEXT("targetPinName"));
		if (DstNodeId.IsEmpty() || DstPinName.IsEmpty())
		{ Entry->SetStringField(TEXT("error"), TEXT("connect requires targetNodeId and targetPinName"));
	return;}
		UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, DstNodeId);
		if (!DstNode) { Entry->SetStringField(TEXT("error"), TEXT("Target node not found"));
	return;}
		UEdGraphPin* DstPin = FNexusBlueprintGraphUtils::FindBPPin(DstNode, DstPinName);
		if (!DstPin)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Target pin '%s' not found. Available pinName: %s"),
				*DstPinName, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(DstNode)));
	return;
		}

		SrcPin->Modify();
		DstPin->Modify();

		const FPinConnectionResponse Resp = Schema->CanCreateConnection(SrcPin, DstPin);
		if (Resp.Response == CONNECT_RESPONSE_DISALLOW)
		{ Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Cannot connect: %s"), *Resp.Message.ToString()));
	return;}

		if (!Schema->TryCreateConnection(SrcPin, DstPin))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("TryCreateConnection failed (src %s.%s -> dst %s.%s). Use then->execute for exec; match data pin types"),
				*SrcNodeId, *SrcPin->PinName.ToString(), *DstNodeId, *DstPin->PinName.ToString()));
	return;
		}
		Entry->SetStringField(TEXT("targetNodeId"),  DstNodeId);
		Entry->SetStringField(TEXT("targetPinName"), DstPin->PinName.ToString());
		Entry->SetStringField(TEXT("sourcePinName"), SrcPin->PinName.ToString());
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return;
	}

	if (Action == TEXT("disconnect"))
	{
		const FString DstNodeId  = FNexusArgs(Op).Str(TEXT("targetNodeId"));
		const FString DstPinName = FNexusArgs(Op).Str(TEXT("targetPinName"));
		if (DstNodeId.IsEmpty() || DstPinName.IsEmpty())
		{ Entry->SetStringField(TEXT("error"), TEXT("disconnect requires targetNodeId and targetPinName"));
	return;}
		UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, DstNodeId);
		if (!DstNode) { Entry->SetStringField(TEXT("error"), TEXT("Target node not found"));
	return;}
		UEdGraphPin* DstPin = FNexusBlueprintGraphUtils::FindBPPin(DstNode, DstPinName);
		if (!DstPin) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Target pin '%s' not found"), *DstPinName));
	return;}
		Schema->BreakSinglePinLink(SrcPin, DstPin);
		Entry->SetStringField(TEXT("targetPinName"), DstPinName);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return;
	}

	if (Action == TEXT("disconnect_all"))
	{
		const int32 LinkCount = SrcPin->LinkedTo.Num();
		Schema->BreakPinLinks(*SrcPin, true);
		Entry->SetNumberField(TEXT("disconnectedCount"), LinkCount);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}
