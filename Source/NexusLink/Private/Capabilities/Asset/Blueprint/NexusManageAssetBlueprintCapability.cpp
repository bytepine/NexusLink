// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("批量编辑 BP：图/变量/节点/连线、SCS、CDO。SCS/defaults 限 Actor BP。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
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
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("蓝图资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
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
	FCapabilityResult Result = FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
		if (!BP) { FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, FString::Printf(TEXT("Blueprint 未找到: %s"), *AssetPath)); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		const FString Action = OpArgs->HasField(TEXT("action")) ? OpArgs->GetStringField(TEXT("action")).ToLower() : TEXT("");
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);
		if (Action.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("缺少 action"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		// ── Variable actions ─────────────────────────────────────────────────────
		if (Action == TEXT("add_variable") || Action == TEXT("remove_variable"))
		{
			const FString VarName = OpArgs->HasField(TEXT("variableName")) ? OpArgs->GetStringField(TEXT("variableName")) : TEXT("");
			if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("variableName 必填")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
			Entry->SetStringField(TEXT("variableName"), VarName);

			if (Action == TEXT("remove_variable"))
			{
				const FName VarFName(*VarName);
				bool bFound = false;
				for (const FBPVariableDescription& Var : BP->NewVariables)
				{ if (Var.VarName == VarFName) { bFound = true; break; } }
				if (!bFound) { Entry->SetStringField(TEXT("error"), TEXT("变量未找到（或为继承变量）")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				FBlueprintEditorUtils::RemoveMemberVariable(BP, VarFName);
			}
			else
			{
				if (!OpArgs->HasField(TEXT("variableType"))) { Entry->SetStringField(TEXT("error"), TEXT("add_variable 需要 variableType")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				const FString VarTypeRaw = OpArgs->GetStringField(TEXT("variableType"));

				bool bVarExists = false;
				for (const FBPVariableDescription& Var : BP->NewVariables)
				{ if (Var.VarName.ToString() == VarName) { bVarExists = true; break; } }
				if (bVarExists) { Entry->SetStringField(TEXT("error"), TEXT("变量已存在")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				FEdGraphPinType PinType;
				FString TypeErr;
				if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { Entry->SetStringField(TEXT("error"), TypeErr); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);

				if (OpArgs->HasField(TEXT("defaultValue")))
				{
					const FString DefaultVal = OpArgs->GetStringField(TEXT("defaultValue"));
					for (FBPVariableDescription& Var : BP->NewVariables)
					{ if (Var.VarName.ToString() == VarName) { Var.DefaultValue = DefaultVal; break; } }
				}
				if (OpArgs->HasField(TEXT("category")))
				{
					FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, FName(*VarName), nullptr,
						FText::FromString(OpArgs->GetStringField(TEXT("category"))));
				}
				bool bIsPublic = false;
				if (OpArgs->HasField(TEXT("isPublic"))) bIsPublic = OpArgs->GetBoolField(TEXT("isPublic"));
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
				Entry->SetStringField(TEXT("variableType"), VarTypeRaw.ToLower());
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		// ── Actor SCS / CDO actions ──────────────────────────────────────────────
		if (Action == TEXT("add_component") || Action == TEXT("remove_component") ||
		    Action == TEXT("set_component_property") || Action == TEXT("set_defaults"))
		{
			if (!BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()))
			{
				const FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("(none)");
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Blueprint 父类不是 Actor 子类: %s（parent=%s）。提示：add_component/set_defaults 需要 Actor BP；GameplayAbility/UI BP 请用 add_variable/add_node。"),
					*AssetPath, *ParentName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
			}

			if (Action == TEXT("add_component"))
			{
				FString ComponentClassName, ComponentName;
				OpArgs->TryGetStringField(TEXT("componentClass"), ComponentClassName);
				OpArgs->TryGetStringField(TEXT("componentName"),  ComponentName);
				if (ComponentClassName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_component 需要 componentClass")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				if (ComponentName.IsEmpty())      { Entry->SetStringField(TEXT("error"), TEXT("add_component 需要 componentName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
				if (!SCS) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint 无 SimpleConstructionScript")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				UClass* CompClass = FNexusAssetUtils::FindClassWithUPrefix(ComponentClassName);
				if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("组件类未找到或不是 ActorComponent: %s"), *ComponentClassName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
				}
				if (SCS->FindSCSNode(*ComponentName))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("组件 '%s' 已存在"), *ComponentName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
				}

				USCS_Node* NewNode = SCS->CreateNode(CompClass, *ComponentName);
				if (!NewNode) { Entry->SetStringField(TEXT("error"), TEXT("创建 SCS 节点失败")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				FString AttachTo;
				OpArgs->TryGetStringField(TEXT("attachTo"), AttachTo);
				if (!AttachTo.IsEmpty())
				{
					USCS_Node* ParentNode = SCS->FindSCSNode(*AttachTo);
					if (!ParentNode) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("attachTo 组件 '%s' 未找到"), *AttachTo)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
					ParentNode->AddChildNode(NewNode);
				}
				else
				{
					USCS_Node* DefaultRoot = SCS->GetDefaultSceneRootNode();
					if (DefaultRoot) DefaultRoot->AddChildNode(NewNode);
					else             SCS->AddNode(NewNode);
				}

				Entry->SetStringField(TEXT("componentName"),  ComponentName);
				Entry->SetStringField(TEXT("componentClass"), CompClass->GetName());
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
			}

			if (Action == TEXT("remove_component"))
			{
				USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
				if (!SCS) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint 无 SimpleConstructionScript")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				FString ComponentName;
				OpArgs->TryGetStringField(TEXT("componentName"), ComponentName);
				if (ComponentName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("remove_component 需要 componentName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				USCS_Node* Node = SCS->FindSCSNode(*ComponentName);
				if (!Node) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("组件 '%s' 未找到"), *ComponentName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				Entry->SetStringField(TEXT("componentName"), ComponentName);
				SCS->RemoveNodeAndPromoteChildren(Node);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
			}

			if (Action == TEXT("set_component_property"))
			{
				USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
				if (!SCS) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint 无 SimpleConstructionScript")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				FString ComponentName, PropPath, Value;
				OpArgs->TryGetStringField(TEXT("componentName"), ComponentName);
				OpArgs->TryGetStringField(TEXT("propertyPath"),  PropPath);
				OpArgs->TryGetStringField(TEXT("value"),         Value);
				if (ComponentName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("缺少 componentName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				if (PropPath.IsEmpty())      { Entry->SetStringField(TEXT("error"), TEXT("缺少 propertyPath")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				USCS_Node* SCSNode = SCS->FindSCSNode(*ComponentName);
				if (!SCSNode || !SCSNode->ComponentTemplate)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("组件 '%s' 未找到"), *ComponentName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
				}

				UActorComponent* Template = SCSNode->ComponentTemplate;
				TArray<FString> Segments;
				PropPath.ParseIntoArray(Segments, TEXT("."), true);

				FProperty* Prop   = nullptr;
				void*      ValPtr = nullptr;
				FString    PropErr;
				if (!FNexusPropertyUtils::ResolvePropertyWrite(Template, Segments, 0, Prop, ValPtr, PropErr)) { Entry->SetStringField(TEXT("error"), PropErr); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, Template))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("设置 '%s' = '%s' 失败"), *PropPath, *Value)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
				}

				Template->MarkPackageDirty();
				Entry->SetStringField(TEXT("componentName"), ComponentName);
				Entry->SetStringField(TEXT("propertyPath"),  PropPath);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
			}

			// set_defaults
			{
				FString PropPath, Value;
				OpArgs->TryGetStringField(TEXT("propertyPath"), PropPath);
				OpArgs->TryGetStringField(TEXT("value"),        Value);
				if (PropPath.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set_defaults 需要 propertyPath")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				if (!BP->GeneratedClass) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint 无生成类")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				UObject* CDO = BP->GeneratedClass->GetDefaultObject();
				if (!CDO) { Entry->SetStringField(TEXT("error"), TEXT("获取 CDO 失败")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				TArray<FString> Segments;
				PropPath.ParseIntoArray(Segments, TEXT("."), true);

				FProperty* Prop   = nullptr;
				void*      ValPtr = nullptr;
				FString    PropErr;
				if (!FNexusPropertyUtils::ResolvePropertyWrite(CDO, Segments, 0, Prop, ValPtr, PropErr)) { Entry->SetStringField(TEXT("error"), PropErr); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, CDO))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("设置 '%s' = '%s' 失败"), *PropPath, *Value));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				Entry->SetStringField(TEXT("propertyPath"), PropPath);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				FKismetEditorUtilities::CompileBlueprint(BP);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
		}

		// ── Graph / Wire actions: require graphName ───────────────────────────────
		const FString GraphName = OpArgs->HasField(TEXT("graphName")) ? OpArgs->GetStringField(TEXT("graphName")) : TEXT("");
		if (GraphName.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("节点/连线操作需要 graphName。提示：先 get_asset_blueprint(sections=[\"graphOverview\"]) 列出图名。")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
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
				TEXT("Graph '%s' 未找到。提示：graphName 是图对象名而非函数名。可用: %s"),
				*GraphName, *FString::Join(GraphNames, TEXT(", ")))); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		Entry->SetStringField(TEXT("graphName"), GraphName);
		BP->Modify();
		Graph->Modify();

		// ── Node actions ──────────────────────────────────────────────────────────
		if (Action == TEXT("add_node"))
		{
			if (!OpArgs->HasField(TEXT("nodeClass"))) { Entry->SetStringField(TEXT("error"), TEXT("add_node 需要 nodeClass")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
			const FString NodeClass = OpArgs->GetStringField(TEXT("nodeClass"));
			const int32 PosX = OpArgs->HasField(TEXT("posX")) ? static_cast<int32>(OpArgs->GetNumberField(TEXT("posX"))) : 0;
			const int32 PosY = OpArgs->HasField(TEXT("posY")) ? static_cast<int32>(OpArgs->GetNumberField(TEXT("posY"))) : 0;

			UEdGraphNode* NewNode = nullptr;

			if (NodeClass == TEXT("K2Node_CallFunction"))
			{
				if (!OpArgs->HasField(TEXT("functionName"))) { Entry->SetStringField(TEXT("error"), TEXT("K2Node_CallFunction 需要 functionName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				const FString FuncName  = OpArgs->GetStringField(TEXT("functionName"));
				const FString FuncClass = OpArgs->HasField(TEXT("functionClass")) ? OpArgs->GetStringField(TEXT("functionClass")) : TEXT("");

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
				if (!Func) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("函数 '%s' 未找到（已尝试 'K2_%s'）"), *FuncName, *FuncName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

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
				if (!OpArgs->HasField(TEXT("variableName"))) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("%s 需要 variableName"), *NodeClass)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				const FString VarName = OpArgs->GetStringField(TEXT("variableName"));

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
				{ Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("节点类 '%s' 未找到"), *NodeClass)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
				UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeUClass);
				Node->SetFlags(RF_Transactional);
				Graph->AddNode(Node, false, false);
				Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
				Node->NodePosX = PosX; Node->NodePosY = PosY;
				NewNode = Node;
			}

		Entry->SetStringField(TEXT("nodeId"),    NewNode->NodeGuid.ToString());
		Entry->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
		Entry->SetStringField(TEXT("nodeTitle"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		if (Action == TEXT("remove_node"))
		{
			if (!OpArgs->HasField(TEXT("nodeId"))) { Entry->SetStringField(TEXT("error"), TEXT("remove_node 需要 nodeId")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
			const FString NodeIdStr = OpArgs->GetStringField(TEXT("nodeId"));
			UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
			if (!Node) { Entry->SetStringField(TEXT("error"), TEXT("节点未找到")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
			Entry->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			Node->Modify();
			Graph->GetSchema()->BreakNodeLinks(*Node);
			Graph->RemoveNode(Node);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		if (Action == TEXT("set_node"))
		{
			if (!OpArgs->HasField(TEXT("nodeId"))) { Entry->SetStringField(TEXT("error"), TEXT("set_node 需要 nodeId")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
			const FString NodeIdStr = OpArgs->GetStringField(TEXT("nodeId"));
			UEdGraphNode* Node = FNexusBlueprintGraphUtils::FindBPNode(Graph, NodeIdStr);
			if (!Node) { Entry->SetStringField(TEXT("error"), TEXT("节点未找到")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

			Node->Modify();
			if (OpArgs->HasField(TEXT("posX")))    Node->NodePosX = static_cast<int32>(OpArgs->GetNumberField(TEXT("posX")));
			if (OpArgs->HasField(TEXT("posY")))    Node->NodePosY = static_cast<int32>(OpArgs->GetNumberField(TEXT("posY")));
			if (OpArgs->HasField(TEXT("comment"))) Node->NodeComment = OpArgs->GetStringField(TEXT("comment"));

			if (OpArgs->HasField(TEXT("pinName")) && OpArgs->HasField(TEXT("pinDefaultValue")))
			{
				const FString PinName = OpArgs->GetStringField(TEXT("pinName"));
				const FString PinVal  = OpArgs->GetStringField(TEXT("pinDefaultValue"));
				bool bFound = false;
				for (UEdGraphPin* Pin : Node->Pins)
				{ if (Pin && Pin->PinName.ToString() == PinName) { Pin->DefaultValue = PinVal; bFound = true; break; } }
				if (!bFound) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("引脚 '%s' 未找到"), *PinName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }
			}

		Entry->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
	}

	// ── Wire actions ──────────────────────────────────────────────────────────
		const FString SrcNodeId  = OpArgs->HasField(TEXT("sourceNodeId"))  ? OpArgs->GetStringField(TEXT("sourceNodeId"))  : TEXT("");
		const FString SrcPinName = OpArgs->HasField(TEXT("sourcePinName")) ? OpArgs->GetStringField(TEXT("sourcePinName")) : TEXT("");
		if (SrcNodeId.IsEmpty() || SrcPinName.IsEmpty())
		{ Entry->SetStringField(TEXT("error"), TEXT("连线操作需要 sourceNodeId 与 sourcePinName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

		UEdGraphNode* SrcNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, SrcNodeId);
		if (!SrcNode) { Entry->SetStringField(TEXT("error"), TEXT("源节点未找到")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}
		UEdGraphPin* SrcPin = FNexusBlueprintGraphUtils::FindBPPin(SrcNode, SrcPinName);
		if (!SrcPin)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("源引脚 '%s' 未找到。可用 pinName: %s"),
				*SrcPinName, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(SrcNode))); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		Entry->SetStringField(TEXT("sourceNodeId"),  SrcNodeId);
		Entry->SetStringField(TEXT("sourcePinName"), SrcPinName);

		const UEdGraphSchema* Schema = Graph->GetSchema();

		if (Action == TEXT("connect"))
		{
			if (!OpArgs->HasField(TEXT("targetNodeId")) || !OpArgs->HasField(TEXT("targetPinName")))
			{ Entry->SetStringField(TEXT("error"), TEXT("connect 需要 targetNodeId 与 targetPinName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}
			const FString DstNodeId  = OpArgs->GetStringField(TEXT("targetNodeId"));
			const FString DstPinName = OpArgs->GetStringField(TEXT("targetPinName"));
			UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, DstNodeId);
			if (!DstNode) { Entry->SetStringField(TEXT("error"), TEXT("目标节点未找到")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}
			UEdGraphPin* DstPin = FNexusBlueprintGraphUtils::FindBPPin(DstNode, DstPinName);
			if (!DstPin)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("目标引脚 '%s' 未找到。可用 pinName: %s"),
					*DstPinName, *FNexusBlueprintGraphUtils::FormatBPPinNameHint(DstNode))); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
			}

			SrcPin->Modify();
			DstPin->Modify();

			const FPinConnectionResponse Resp = Schema->CanCreateConnection(SrcPin, DstPin);
			if (Resp.Response == CONNECT_RESPONSE_DISALLOW)
			{ Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("无法连接: %s"), *Resp.Message.ToString())); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}

			if (!Schema->TryCreateConnection(SrcPin, DstPin))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("TryCreateConnection 失败（源 %s.%s → 目标 %s.%s）。请确认 exec 用 then→execute、数据引脚类型兼容"),
					*SrcNodeId, *SrcPin->PinName.ToString(), *DstNodeId, *DstPin->PinName.ToString())); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
			}
			Entry->SetStringField(TEXT("targetNodeId"),  DstNodeId);
			Entry->SetStringField(TEXT("targetPinName"), DstPin->PinName.ToString());
			Entry->SetStringField(TEXT("sourcePinName"), SrcPin->PinName.ToString());
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		if (Action == TEXT("disconnect"))
		{
			if (!OpArgs->HasField(TEXT("targetNodeId")) || !OpArgs->HasField(TEXT("targetPinName")))
			{ Entry->SetStringField(TEXT("error"), TEXT("disconnect 需要 targetNodeId 与 targetPinName")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}
			UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, OpArgs->GetStringField(TEXT("targetNodeId")));
			if (!DstNode) { Entry->SetStringField(TEXT("error"), TEXT("目标节点未找到")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}
			const FString DstPinName = OpArgs->GetStringField(TEXT("targetPinName"));
			UEdGraphPin* DstPin = FNexusBlueprintGraphUtils::FindBPPin(DstNode, DstPinName);
			if (!DstPin) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("目标引脚 '%s' 未找到"), *DstPinName)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;}
			Schema->BreakSinglePinLink(SrcPin, DstPin);
			Entry->SetStringField(TEXT("targetPinName"), DstPinName);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		if (Action == TEXT("disconnect_all"))
		{
			const int32 LinkCount = SrcPin->LinkedTo.Num();
			Schema->BreakPinLinks(*SrcPin, true);
			Entry->SetNumberField(TEXT("disconnectedCount"), LinkCount);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue;
		}

		Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action '%s'"), *Action));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});

	return Result;
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& Entry, auto& OutError)
	{
		OutError = TEXT("manage_asset_blueprint 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetBlueprintCapability)
