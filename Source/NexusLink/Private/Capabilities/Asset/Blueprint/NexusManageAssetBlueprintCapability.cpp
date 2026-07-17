// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPinTypeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
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
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif
#include "UObject/UObjectIterator.h"
#include "NexusMcpTool.h"


void FManageAssetBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_blueprint");
	Out.SearchAssetTypes = {TEXT("Blueprint")};
	Out.Description = TEXT("编辑 BP：图/变量/节点/连线、SCS、CDO。SCS/defaults 限 Actor BP。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),       FNexusSchema::Str(TEXT("蓝图资产路径")))
		.Prop(TEXT("action"),          FNexusSchema::Enum(TEXT("操作类型"), {
			TEXT("add_variable"), TEXT("remove_variable"),
			TEXT("add_node"), TEXT("remove_node"), TEXT("set_node"),
			TEXT("connect"), TEXT("disconnect"), TEXT("disconnect_all"),
			TEXT("add_component"), TEXT("remove_component"), TEXT("set_component_property"), TEXT("set_defaults")
		}))
		.Prop(TEXT("graphName"),       FNexusSchema::Str(TEXT("图名（节点/连线操作）")))
		.Prop(TEXT("variableName"),    FNexusSchema::Str(TEXT("变量或节点变量名")))
		.Prop(TEXT("variableType"),    FNexusSchema::Str(TEXT("基本或对象类型（add_variable）")))
		.Prop(TEXT("defaultValue"),    FNexusSchema::Str(TEXT("默认值（add_variable）")))
		.Prop(TEXT("category"),        FNexusSchema::Str(TEXT("编辑器分类（add_variable）")))
		.Prop(TEXT("isPublic"),        FNexusSchema::Bool(TEXT("实例可编辑（add_variable）"), true, false))
		.Prop(TEXT("nodeId"),          FNexusSchema::Str(TEXT("节点 GUID（remove/set_node）")))
		.Prop(TEXT("nodeClass"),       FNexusSchema::Str(TEXT("K2Node 类（add_node）")))
		.Prop(TEXT("functionName"),    FNexusSchema::Str(TEXT("CallFunction：函数名")))
		.Prop(TEXT("functionClass"),   FNexusSchema::Str(TEXT("CallFunction：所属类")))
		.Prop(TEXT("posX"),            FNexusSchema::Num(TEXT("节点 X 坐标")))
		.Prop(TEXT("posY"),            FNexusSchema::Num(TEXT("节点 Y 坐标")))
		.Prop(TEXT("comment"),         FNexusSchema::Str(TEXT("节点注释（set_node）")))
		.Prop(TEXT("pinName"),         FNexusSchema::Str(TEXT("要设默认值的引脚（set_node）")))
		.Prop(TEXT("pinDefaultValue"), FNexusSchema::Str(TEXT("引脚新默认值")))
		.Prop(TEXT("sourceNodeId"),    FNexusSchema::Str(TEXT("源节点 GUID（连线操作）")))
		.Prop(TEXT("sourcePinName"),   FNexusSchema::Str(TEXT("源引脚名")))
		.Prop(TEXT("targetNodeId"),    FNexusSchema::Str(TEXT("目标节点 GUID（connect/disconnect）")))
		.Prop(TEXT("targetPinName"),   FNexusSchema::Str(TEXT("目标引脚名")))
		.Prop(TEXT("componentClass"),  FNexusSchema::Str(TEXT("组件类名（add_component），如 StaticMeshComponent")))
		.Prop(TEXT("componentName"),   FNexusSchema::Str(TEXT("SCS 变量名（add/remove/set_component_property）")))
		.Prop(TEXT("attachTo"),        FNexusSchema::Str(TEXT("父组件变量名（add_component）；省略则用默认场景根")))
		.Prop(TEXT("propertyPath"),    FNexusSchema::Str(TEXT("属性路径，点分记法如 RelativeLocation.X（set_component_property/set_defaults）")))
		.Prop(TEXT("value"),           FNexusSchema::Str(TEXT("字符串值，如 (X=100,Y=0,Z=50) 或 true（set_component_property/set_defaults）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("variable"), TEXT("node"), TEXT("component"), TEXT("wire"), TEXT("connect"),
		TEXT("disconnect"), TEXT("link"), TEXT("scs")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_blueprint"), TEXT("create_asset_blueprint"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删变量、图节点、连线");
}

FCapabilityResult FManageAssetBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	bool bArgInvalid = false;

	FCapabilityResult Result = FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		auto SetArgInvalid = [&](const FString& Msg)
		{
			OutError = Msg;
			bArgInvalid = true;
		};

		const FString AssetPath = Arguments->HasField(TEXT("assetPath")) ? Arguments->GetStringField(TEXT("assetPath")) : TEXT("");
		const FString Action    = Arguments->HasField(TEXT("action"))    ? Arguments->GetStringField(TEXT("action")).ToLower() : TEXT("");
		if (AssetPath.IsEmpty()) { SetArgInvalid(TEXT("assetPath 为必填项")); return; }
		if (Action.IsEmpty())    { SetArgInvalid(TEXT("缺少 action")); return; }

		UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
		if (!BP) { OutError = FString::Printf(TEXT("Blueprint 未找到: %s"), *AssetPath); return; }

		OutTop->SetStringField(TEXT("action"), Action);

		// ── Variable actions ─────────────────────────────────────────────────────
		if (Action == TEXT("add_variable") || Action == TEXT("remove_variable"))
		{
			const FString VarName = Arguments->HasField(TEXT("variableName")) ? Arguments->GetStringField(TEXT("variableName")) : TEXT("");
			if (VarName.IsEmpty()) { SetArgInvalid(TEXT("variableName 必填")); return; }
			OutTop->SetStringField(TEXT("variableName"), VarName);

			if (Action == TEXT("remove_variable"))
			{
				const FName VarFName(*VarName);
				bool bFound = false;
				for (const FBPVariableDescription& Var : BP->NewVariables)
				{ if (Var.VarName == VarFName) { bFound = true; break; } }
				if (!bFound) { OutError = TEXT("变量未找到（或为继承变量）"); return; }
				FBlueprintEditorUtils::RemoveMemberVariable(BP, VarFName);
			}
			else
			{
				if (!Arguments->HasField(TEXT("variableType"))) { SetArgInvalid(TEXT("add_variable 需要 variableType")); return; }
				const FString VarTypeRaw = Arguments->GetStringField(TEXT("variableType"));

				for (const FBPVariableDescription& Var : BP->NewVariables)
				{ if (Var.VarName.ToString() == VarName) { OutError = TEXT("变量已存在"); return; } }

				FEdGraphPinType PinType;
				FString TypeErr;
				if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { OutError = TypeErr; return; }

				FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);

				if (Arguments->HasField(TEXT("defaultValue")))
				{
					const FString DefaultVal = Arguments->GetStringField(TEXT("defaultValue"));
					for (FBPVariableDescription& Var : BP->NewVariables)
					{ if (Var.VarName.ToString() == VarName) { Var.DefaultValue = DefaultVal; break; } }
				}
				if (Arguments->HasField(TEXT("category")))
				{
					FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, FName(*VarName), nullptr,
						FText::FromString(Arguments->GetStringField(TEXT("category"))));
				}
				bool bIsPublic = false;
				if (Arguments->HasField(TEXT("isPublic"))) bIsPublic = Arguments->GetBoolField(TEXT("isPublic"));
				for (FBPVariableDescription& Var : BP->NewVariables)
				{
					if (Var.VarName.ToString() != VarName) continue;
					if (bIsPublic)
					{
#if NX_UE_HAS_CPF_BLUEPRINT_READWRITE
						Var.PropertyFlags |= CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadWrite;
#else
						Var.PropertyFlags |= CPF_Edit | CPF_BlueprintVisible;
						Var.PropertyFlags &= ~CPF_BlueprintReadOnly;
#endif
					}
					else
						Var.PropertyFlags &= ~(CPF_Edit | CPF_ExposeOnSpawn);
					break;
				}
				OutTop->SetStringField(TEXT("variableType"), VarTypeRaw.ToLower());
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			return;
		}

		// ── Actor SCS / CDO actions ──────────────────────────────────────────────
		if (Action == TEXT("add_component") || Action == TEXT("remove_component") ||
		    Action == TEXT("set_component_property") || Action == TEXT("set_defaults"))
		{
			if (!BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()))
			{
				const FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("(none)");
				OutError = FString::Printf(
					TEXT("Blueprint 父类不是 Actor 子类: %s（parent=%s）。提示：add_component/set_defaults 需要 Actor BP；GameplayAbility/UI BP 请用 add_variable/add_node。"),
					*AssetPath, *ParentName);
				return;
			}

			if (Action == TEXT("add_component"))
			{
				FString ComponentClassName, ComponentName;
				Arguments->TryGetStringField(TEXT("componentClass"), ComponentClassName);
				Arguments->TryGetStringField(TEXT("componentName"),  ComponentName);
				if (ComponentClassName.IsEmpty()) { SetArgInvalid(TEXT("add_component 需要 componentClass")); return; }
				if (ComponentName.IsEmpty())      { SetArgInvalid(TEXT("add_component 需要 componentName")); return; }

				USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
				if (!SCS) { OutError = TEXT("Blueprint 无 SimpleConstructionScript"); return; }

				UClass* CompClass = FNexusAssetUtils::FindClassWithUPrefix(ComponentClassName);
				if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
				{
					OutError = FString::Printf(TEXT("组件类未找到或不是 ActorComponent: %s"), *ComponentClassName);
					return;
				}
				if (SCS->FindSCSNode(*ComponentName))
				{
					OutError = FString::Printf(TEXT("组件 '%s' 已存在"), *ComponentName);
					return;
				}

				USCS_Node* NewNode = SCS->CreateNode(CompClass, *ComponentName);
				if (!NewNode) { OutError = TEXT("创建 SCS 节点失败"); return; }

				FString AttachTo;
				Arguments->TryGetStringField(TEXT("attachTo"), AttachTo);
				if (!AttachTo.IsEmpty())
				{
					USCS_Node* ParentNode = SCS->FindSCSNode(*AttachTo);
					if (!ParentNode) { OutError = FString::Printf(TEXT("attachTo 组件 '%s' 未找到"), *AttachTo); return; }
					ParentNode->AddChildNode(NewNode);
				}
				else
				{
					USCS_Node* DefaultRoot = SCS->GetDefaultSceneRootNode();
					if (DefaultRoot) DefaultRoot->AddChildNode(NewNode);
					else             SCS->AddNode(NewNode);
				}

				OutTop->SetStringField(TEXT("componentName"),  ComponentName);
				OutTop->SetStringField(TEXT("componentClass"), CompClass->GetName());
				OutTop->SetBoolField(TEXT("success"), true);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				return;
			}

			if (Action == TEXT("remove_component"))
			{
				USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
				if (!SCS) { OutError = TEXT("Blueprint 无 SimpleConstructionScript"); return; }

				FString ComponentName;
				Arguments->TryGetStringField(TEXT("componentName"), ComponentName);
				if (ComponentName.IsEmpty()) { OutError = TEXT("remove_component 需要 componentName"); return; }

				USCS_Node* Node = SCS->FindSCSNode(*ComponentName);
				if (!Node) { OutError = FString::Printf(TEXT("组件 '%s' 未找到"), *ComponentName); return; }

				OutTop->SetStringField(TEXT("componentName"), ComponentName);
				OutTop->SetBoolField(TEXT("success"), true);
				SCS->RemoveNodeAndPromoteChildren(Node);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				return;
			}

			if (Action == TEXT("set_component_property"))
			{
				USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
				if (!SCS) { OutError = TEXT("Blueprint 无 SimpleConstructionScript"); return; }

				FString ComponentName, PropPath, Value;
				Arguments->TryGetStringField(TEXT("componentName"), ComponentName);
				Arguments->TryGetStringField(TEXT("propertyPath"),  PropPath);
				Arguments->TryGetStringField(TEXT("value"),         Value);
				if (ComponentName.IsEmpty()) { OutError = TEXT("缺少 componentName"); return; }
				if (PropPath.IsEmpty())      { OutError = TEXT("缺少 propertyPath"); return; }

				USCS_Node* SCSNode = SCS->FindSCSNode(*ComponentName);
				if (!SCSNode || !SCSNode->ComponentTemplate)
				{
					OutError = FString::Printf(TEXT("组件 '%s' 未找到"), *ComponentName);
					return;
				}

				UActorComponent* Template = SCSNode->ComponentTemplate;
				TArray<FString> Segments;
				PropPath.ParseIntoArray(Segments, TEXT("."), true);

				FProperty* Prop   = nullptr;
				void*      ValPtr = nullptr;
				FString    PropErr;
				if (!FNexusPropertyUtils::ResolvePropertyWrite(Template, Segments, 0, Prop, ValPtr, PropErr)) { OutError = PropErr; return; }
				if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, Template))
				{
					OutError = FString::Printf(TEXT("设置 '%s' = '%s' 失败"), *PropPath, *Value);
					return;
				}

				Template->MarkPackageDirty();
				OutTop->SetStringField(TEXT("componentName"), ComponentName);
				OutTop->SetStringField(TEXT("propertyPath"),  PropPath);
				OutTop->SetBoolField(TEXT("success"), true);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				return;
			}

			// set_defaults
			{
				FString PropPath, Value;
				Arguments->TryGetStringField(TEXT("propertyPath"), PropPath);
				Arguments->TryGetStringField(TEXT("value"),        Value);
				if (PropPath.IsEmpty()) { SetArgInvalid(TEXT("set_defaults 需要 propertyPath")); return; }
				if (!BP->GeneratedClass) { OutError = TEXT("Blueprint 无生成类"); return; }

				UObject* CDO = BP->GeneratedClass->GetDefaultObject();
				if (!CDO) { OutError = TEXT("获取 CDO 失败"); return; }

				TArray<FString> Segments;
				PropPath.ParseIntoArray(Segments, TEXT("."), true);

				FProperty* Prop   = nullptr;
				void*      ValPtr = nullptr;
				FString    PropErr;
				if (!FNexusPropertyUtils::ResolvePropertyWrite(CDO, Segments, 0, Prop, ValPtr, PropErr)) { OutError = PropErr; return; }
				if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, CDO))
				{
					OutError = FString::Printf(TEXT("设置 '%s' = '%s' 失败"), *PropPath, *Value);
					return;
				}

				OutTop->SetStringField(TEXT("propertyPath"), PropPath);
				OutTop->SetBoolField(TEXT("success"), true);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				return;
			}
		}

		// ── Graph / Wire actions: require graphName ───────────────────────────────
		const FString GraphName = Arguments->HasField(TEXT("graphName")) ? Arguments->GetStringField(TEXT("graphName")) : TEXT("");
		if (GraphName.IsEmpty())
		{
			SetArgInvalid(TEXT("节点/连线操作需要 graphName。提示：先 get_asset_blueprint(sections=[\"graphOverview\"]) 列出图名。"));
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
			OutError = FString::Printf(
				TEXT("Graph '%s' 未找到。提示：graphName 是图对象名而非函数名。可用: %s"),
				*GraphName, *FString::Join(GraphNames, TEXT(", ")));
			return;
		}

		OutTop->SetStringField(TEXT("graphName"), GraphName);
		BP->Modify();
		Graph->Modify();

		// ── Node actions ──────────────────────────────────────────────────────────
		if (Action == TEXT("add_node"))
		{
			if (!Arguments->HasField(TEXT("nodeClass"))) { OutError = TEXT("add_node 需要 nodeClass"); return; }
			const FString NodeClass = Arguments->GetStringField(TEXT("nodeClass"));
			const int32 PosX = Arguments->HasField(TEXT("posX")) ? static_cast<int32>(Arguments->GetNumberField(TEXT("posX"))) : 0;
			const int32 PosY = Arguments->HasField(TEXT("posY")) ? static_cast<int32>(Arguments->GetNumberField(TEXT("posY"))) : 0;

			UEdGraphNode* NewNode = nullptr;

			if (NodeClass == TEXT("K2Node_CallFunction"))
			{
				if (!Arguments->HasField(TEXT("functionName"))) { OutError = TEXT("K2Node_CallFunction 需要 functionName"); return; }
				const FString FuncName  = Arguments->GetStringField(TEXT("functionName"));
				const FString FuncClass = Arguments->HasField(TEXT("functionClass")) ? Arguments->GetStringField(TEXT("functionClass")) : TEXT("");

				UFunction* Func = nullptr;
				if (!FuncClass.IsEmpty())
				{
					UClass* C = FNexusAssetUtils::FindClassWithUPrefix(FuncClass);
					if (C) Func = C->FindFunctionByName(*FuncName);
				}
				if (!Func)
				{
					int32 N = 0;
					for (TObjectIterator<UClass> It; It && N < 5000; ++It, ++N) { Func = It->FindFunctionByName(*FuncName); if (Func) break; }
				}
				if (!Func)
				{
					const FString K2Name = TEXT("K2_") + FuncName;
					int32 N = 0;
					for (TObjectIterator<UClass> It; It && N < 5000; ++It, ++N) { Func = It->FindFunctionByName(*K2Name); if (Func) break; }
				}
				if (!Func) { OutError = FString::Printf(TEXT("函数 '%s' 未找到（已尝试 'K2_%s'）"), *FuncName, *FuncName); return; }

				UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
				Node->SetFlags(RF_Transactional);
				Node->SetFromFunction(Func);
				Graph->AddNode(Node, false, false);
				Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
				Node->NodePosX = PosX; Node->NodePosY = PosY;
				NewNode = Node;
			}
			else if (NodeClass == TEXT("K2Node_VariableGet") || NodeClass == TEXT("K2Node_VariableSet"))
			{
				if (!Arguments->HasField(TEXT("variableName"))) { OutError = FString::Printf(TEXT("%s 需要 variableName"), *NodeClass); return; }
				const FString VarName = Arguments->GetStringField(TEXT("variableName"));

				if (NodeClass == TEXT("K2Node_VariableGet"))
				{
					UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
					Node->SetFlags(RF_Transactional);
					Node->VariableReference.SetSelfMember(FName(*VarName));
					Graph->AddNode(Node, false, false);
					Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
					Node->NodePosX = PosX; Node->NodePosY = PosY;
					NewNode = Node;
				}
				else
				{
					UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(Graph);
					Node->SetFlags(RF_Transactional);
					Node->VariableReference.SetSelfMember(FName(*VarName));
					Graph->AddNode(Node, false, false);
					Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
					Node->NodePosX = PosX; Node->NodePosY = PosY;
					NewNode = Node;
				}
			}
			else
			{
				UClass* NodeUClass = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
				if (!NodeUClass || !NodeUClass->IsChildOf(UEdGraphNode::StaticClass()))
				{ OutError = FString::Printf(TEXT("节点类 '%s' 未找到"), *NodeClass); return; }
				UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeUClass);
				Node->SetFlags(RF_Transactional);
				Graph->AddNode(Node, false, false);
				Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
				Node->NodePosX = PosX; Node->NodePosY = PosY;
				NewNode = Node;
			}

		OutTop->SetStringField(TEXT("nodeId"),    NewNode->NodeGuid.ToString());
		OutTop->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
		OutTop->SetStringField(TEXT("nodeTitle"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		return;
		}

		if (Action == TEXT("remove_node"))
		{
			if (!Arguments->HasField(TEXT("nodeId"))) { OutError = TEXT("remove_node 需要 nodeId"); return; }
			const FString NodeIdStr = Arguments->GetStringField(TEXT("nodeId"));
			UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
			if (!Node) { OutError = TEXT("节点未找到"); return; }
			OutTop->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			Node->Modify();
			Graph->GetSchema()->BreakNodeLinks(*Node);
			Graph->RemoveNode(Node);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			return;
		}

		if (Action == TEXT("set_node"))
		{
			if (!Arguments->HasField(TEXT("nodeId"))) { OutError = TEXT("set_node 需要 nodeId"); return; }
			const FString NodeIdStr = Arguments->GetStringField(TEXT("nodeId"));
			UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
			if (!Node) { OutError = TEXT("节点未找到"); return; }

			Node->Modify();
			if (Arguments->HasField(TEXT("posX")))    Node->NodePosX = static_cast<int32>(Arguments->GetNumberField(TEXT("posX")));
			if (Arguments->HasField(TEXT("posY")))    Node->NodePosY = static_cast<int32>(Arguments->GetNumberField(TEXT("posY")));
			if (Arguments->HasField(TEXT("comment"))) Node->NodeComment = Arguments->GetStringField(TEXT("comment"));

			if (Arguments->HasField(TEXT("pinName")) && Arguments->HasField(TEXT("pinDefaultValue")))
			{
				const FString PinName = Arguments->GetStringField(TEXT("pinName"));
				const FString PinVal  = Arguments->GetStringField(TEXT("pinDefaultValue"));
				bool bFound = false;
				for (UEdGraphPin* Pin : Node->Pins)
				{ if (Pin && Pin->PinName.ToString() == PinName) { Pin->DefaultValue = PinVal; bFound = true; break; } }
				if (!bFound) { OutError = FString::Printf(TEXT("引脚 '%s' 未找到"), *PinName); return; }
			}

		OutTop->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		return;
	}

	// ── Wire actions ──────────────────────────────────────────────────────────
		const FString SrcNodeId  = Arguments->HasField(TEXT("sourceNodeId"))  ? Arguments->GetStringField(TEXT("sourceNodeId"))  : TEXT("");
		const FString SrcPinName = Arguments->HasField(TEXT("sourcePinName")) ? Arguments->GetStringField(TEXT("sourcePinName")) : TEXT("");
		if (SrcNodeId.IsEmpty() || SrcPinName.IsEmpty())
		{ OutError = TEXT("连线操作需要 sourceNodeId 与 sourcePinName"); return; }

		UEdGraphNode* SrcNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, SrcNodeId);
		if (!SrcNode) { OutError = TEXT("源节点未找到"); return; }
		UEdGraphPin* SrcPin = FNexusBlueprintGraphUtils::FindBPPin(SrcNode, SrcPinName);
		if (!SrcPin)
		{
			OutError = FString::Printf(
				TEXT("源引脚 '%s' 未找到。可用 pinName: %s"),
				*SrcPinName, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(SrcNode));
			return;
		}

		OutTop->SetStringField(TEXT("sourceNodeId"),  SrcNodeId);
		OutTop->SetStringField(TEXT("sourcePinName"), SrcPinName);

		const UEdGraphSchema* Schema = Graph->GetSchema();

		if (Action == TEXT("connect"))
		{
			if (!Arguments->HasField(TEXT("targetNodeId")) || !Arguments->HasField(TEXT("targetPinName")))
			{ OutError = TEXT("connect 需要 targetNodeId 与 targetPinName"); return; }
			const FString DstNodeId  = Arguments->GetStringField(TEXT("targetNodeId"));
			const FString DstPinName = Arguments->GetStringField(TEXT("targetPinName"));
			UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, DstNodeId);
			if (!DstNode) { OutError = TEXT("目标节点未找到"); return; }
			UEdGraphPin* DstPin = FNexusBlueprintGraphUtils::FindBPPin(DstNode, DstPinName);
			if (!DstPin)
			{
				OutError = FString::Printf(
					TEXT("目标引脚 '%s' 未找到。可用 pinName: %s"),
					*DstPinName, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(DstNode));
				return;
			}

			SrcPin->Modify();
			DstPin->Modify();

			const FPinConnectionResponse Resp = Schema->CanCreateConnection(SrcPin, DstPin);
			if (Resp.Response == CONNECT_RESPONSE_DISALLOW)
			{ OutError = FString::Printf(TEXT("无法连接: %s"), *Resp.Message.ToString()); return; }

			if (!Schema->TryCreateConnection(SrcPin, DstPin))
			{
				OutError = FString::Printf(
					TEXT("TryCreateConnection 失败（源 %s.%s → 目标 %s.%s）。请确认 exec 用 then→execute、数据引脚类型兼容"),
					*SrcNodeId, *SrcPin->PinName.ToString(), *DstNodeId, *DstPin->PinName.ToString());
				return;
			}
			OutTop->SetStringField(TEXT("targetNodeId"),  DstNodeId);
			OutTop->SetStringField(TEXT("targetPinName"), DstPin->PinName.ToString());
			OutTop->SetStringField(TEXT("sourcePinName"), SrcPin->PinName.ToString());
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			return;
		}

		if (Action == TEXT("disconnect"))
		{
			if (!Arguments->HasField(TEXT("targetNodeId")) || !Arguments->HasField(TEXT("targetPinName")))
			{ OutError = TEXT("disconnect 需要 targetNodeId 与 targetPinName"); return; }
			UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, Arguments->GetStringField(TEXT("targetNodeId")));
			if (!DstNode) { OutError = TEXT("目标节点未找到"); return; }
			const FString DstPinName = Arguments->GetStringField(TEXT("targetPinName"));
			UEdGraphPin* DstPin = FNexusBlueprintGraphUtils::FindBPPin(DstNode, DstPinName);
			if (!DstPin) { OutError = FString::Printf(TEXT("目标引脚 '%s' 未找到"), *DstPinName); return; }
			Schema->BreakSinglePinLink(SrcPin, DstPin);
			OutTop->SetStringField(TEXT("targetPinName"), DstPinName);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			return;
		}

		if (Action == TEXT("disconnect_all"))
		{
			const int32 LinkCount = SrcPin->LinkedTo.Num();
			Schema->BreakPinLinks(*SrcPin, true);
			OutTop->SetNumberField(TEXT("disconnectedCount"), LinkCount);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			return;
		}

		OutError = FString::Printf(TEXT("未知 action '%s'"), *Action);

	});

	if (bArgInvalid)
	{
		Result.bIsArgInvalid = true;
	}
	return Result;
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("manage_asset_blueprint 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetBlueprintCapability)
