// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintCapability.h"
#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintGraph.h"
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

#if WITH_EDITOR
/** 解析接口 UClass：类名、生成类路径、或 BPI 资产路径。 */
static UClass* ResolveInterfaceClass(const FString& NameOrPath)
{
	if (NameOrPath.IsEmpty()) return nullptr;

	if (UClass* Cls = FNexusAssetUtils::FindClassWithUPrefix(NameOrPath))
	{
		if (Cls->HasAnyClassFlags(CLASS_Interface)) return Cls;
	}

	if (UBlueprint* IfaceBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(NameOrPath))
	{
		if (UClass* Gen = IfaceBP->GeneratedClass)
		{
			if (Gen->HasAnyClassFlags(CLASS_Interface)) return Gen;
		}
	}
	return nullptr;
}

static bool BlueprintAlreadyImplements(const UBlueprint* BP, const UClass* IfaceClass)
{
	if (!BP || !IfaceClass) return false;
	for (const FBPInterfaceDescription& Desc : BP->ImplementedInterfaces)
	{
		if (Desc.Interface == IfaceClass) return true;
	}
	return false;
}
#endif


void FManageAssetBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_blueprint");
	Out.bInjectCompile = true;
	Out.SearchAssetTypes = {TEXT("Blueprint")};
	Out.Description = TEXT("Batch edit BP: graphs/vars/funcs/macros/timelines/dispatchers/interfaces/nodes/promote_pin.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),          FNexusSchema::Enum(TEXT("Operation type"), {
			TEXT("add_variable"), TEXT("remove_variable"),
			TEXT("add_function"), TEXT("remove_function"),
			TEXT("add_macro"), TEXT("add_timeline"), TEXT("add_dispatcher"), TEXT("add_local_variable"),
			TEXT("add_interface"), TEXT("remove_interface"),
			TEXT("add_node"), TEXT("remove_node"), TEXT("set_node"), TEXT("promote_pin"),
			TEXT("connect"), TEXT("disconnect"), TEXT("disconnect_all"),
			TEXT("add_component"), TEXT("remove_component"), TEXT("set_component_property"), TEXT("set_defaults")
		}))
		.Prop(TEXT("graphName"),       FNexusSchema::Str(TEXT("Graph name (nodes/wires/promote_pin)")))
		.Prop(TEXT("variableName"),    FNexusSchema::Str(TEXT("Variable name (add_variable/promote_pin; auto if omitted)")))
		.Prop(TEXT("variableType"),    FNexusSchema::Str(TEXT("Basic or object type (add_variable)")))
		.Prop(TEXT("defaultValue"),    FNexusSchema::Str(TEXT("Default value (add_variable)")))
		.Prop(TEXT("category"),        FNexusSchema::Str(TEXT("Editor category (add_variable)")))
		.Prop(TEXT("isPublic"),        FNexusSchema::Bool(TEXT("Instance editable (add_variable)"), true, false))
		.Prop(TEXT("isLocal"),         FNexusSchema::Bool(TEXT("promote_pin: local var (default member var)"), true, false))
		.Prop(TEXT("nodeId"),          FNexusSchema::Str(TEXT("node GUID (remove/set_node/promote_pin)")))
		.Prop(TEXT("nodeClass"),       FNexusSchema::Str(TEXT("K2Node class (add_node)")))
		.Prop(TEXT("functionName"),    FNexusSchema::Str(TEXT("Function name (add_function/CallFunction)")))
		.Prop(TEXT("functionClass"),   FNexusSchema::Str(TEXT("CallFunction: owner class")))
		.Prop(TEXT("interfaceName"),   FNexusSchema::Str(TEXT("Interface class or BPI path (add/remove_interface)")))
		.Prop(TEXT("posX"),            FNexusSchema::Num(TEXT("Node X position")))
		.Prop(TEXT("posY"),            FNexusSchema::Num(TEXT("Node Y position")))
		.Prop(TEXT("comment"),         FNexusSchema::Str(TEXT("Node comment (set_node)")))
		.Prop(TEXT("pinName"),         FNexusSchema::Str(TEXT("Pin name (set_node/promote_pin)")))
		.Prop(TEXT("pinDefaultValue"), FNexusSchema::Str(TEXT("New pin default value")))
		.Prop(TEXT("sourceNodeId"),    FNexusSchema::Str(TEXT("Source node GUID (wire ops)")))
		.Prop(TEXT("sourcePinName"),   FNexusSchema::Str(TEXT("Source pin name")))
		.Prop(TEXT("targetNodeId"),    FNexusSchema::Str(TEXT("Target node GUID (connect/disconnect)")))
		.Prop(TEXT("targetPinName"),   FNexusSchema::Str(TEXT("Target pin name")))
		.Prop(TEXT("componentClass"),  FNexusSchema::Str(TEXT("Component class (add_component), e.g. StaticMeshComponent")))
		.Prop(TEXT("componentName"),   FNexusSchema::Str(TEXT("SCS variable name (add/remove/set_component_property)")))
		.Prop(TEXT("attachTo"),        FNexusSchema::Str(TEXT("Parent component var (add_component); default scene root if omitted")))
		.Prop(TEXT("propertyPath"),    FNexusSchema::Str(TEXT("Property path, dot notation e.g. RelativeLocation.X")))
		.Prop(TEXT("value"),           FNexusSchema::Str(TEXT("String value e.g. (X=100,Y=0,Z=50) or true")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Blueprint asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("variable"), TEXT("node"), TEXT("component"), TEXT("wire"), TEXT("connect"),
		TEXT("disconnect"), TEXT("link"), TEXT("scs"), TEXT("function"), TEXT("interface"), TEXT("bpi"),
		TEXT("promote"), TEXT("pin")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_blueprint"), TEXT("create_asset_blueprint"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove vars, promote_pin, func graphs, interfaces, nodes, wires");
}

static void HandleBP_Variable(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("add_variable") || Action == TEXT("remove_variable"))
	{
		const FString VarName = FNexusArgs(Op).Str(TEXT("variableName"));
		if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("variableName is required"));
	return; }
		Entry->SetStringField(TEXT("variableName"), VarName);

		if (Action == TEXT("remove_variable"))
		{
			const FName VarFName(*VarName);
			bool bFound = false;
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{ if (Var.VarName == VarFName) { bFound = true; break; } }
			if (!bFound) { Entry->SetStringField(TEXT("error"), TEXT("Variable not found (or inherited)"));
	return; }
			FBlueprintEditorUtils::RemoveMemberVariable(BP, VarFName);
		}
		else
		{
			const FString VarTypeRaw = FNexusArgs(Op).Str(TEXT("variableType"));
			if (VarTypeRaw.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_variable requires variableType"));
	return; }

			bool bVarExists = false;
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{ if (Var.VarName.ToString() == VarName) { bVarExists = true; break; } }
			if (bVarExists) { Entry->SetStringField(TEXT("error"), TEXT("Variable already exists"));
	return; }

			FEdGraphPinType PinType;
			FString TypeErr;
			if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { Entry->SetStringField(TEXT("error"), TypeErr);
	return; }

			BP->SetFlags(RF_Transactional);
			FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);

			if (Op->HasField(TEXT("defaultValue")))
			{
				const FString DefaultVal = Op->GetStringField(TEXT("defaultValue"));
				for (FBPVariableDescription& Var : BP->NewVariables)
				{ if (Var.VarName.ToString() == VarName) { Var.DefaultValue = DefaultVal; break; } }
			}
			if (Op->HasField(TEXT("category")))
			{
				FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, FName(*VarName), nullptr,
					FText::FromString(Op->GetStringField(TEXT("category"))));
			}
			bool bIsPublic = false;
			if (Op->HasField(TEXT("isPublic"))) bIsPublic = Op->GetBoolField(TEXT("isPublic"));
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
		// 全量 Compile 会 CollectGarbage，冲掉进行中的 Undo 快照；compile=true 走 ApplyIfRequested（事务外）
		if (!FNexusEditorTransaction::IsTransactionActive())
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
		}
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}

static void HandleBP_FunctionGraph(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("add_function") || Action == TEXT("remove_function"))
	{
		const FString FuncName = FNexusArgs(Op).Str(TEXT("functionName"));
		if (FuncName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("functionName is required"));
	return; }
		Entry->SetStringField(TEXT("functionName"), FuncName);

		if (Action == TEXT("add_function"))
		{
			bool bExists = false;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == FuncName) { bExists = true; break; }
			}
			if (bExists) { Entry->SetStringField(TEXT("error"), TEXT("Function graph already exists"));
	return; }

			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*FuncName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (!NewGraph) { Entry->SetStringField(TEXT("error"), TEXT("Failed to create function graph"));
	return; }
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromClass=*/nullptr);
		}
		else
		{
			bool bFromInterface = false;
			for (const FBPInterfaceDescription& Desc : BP->ImplementedInterfaces)
			{
				for (UEdGraph* G : Desc.Graphs)
				{
					if (G && G->GetName() == FuncName) { bFromInterface = true; break; }
				}
				if (bFromInterface) break;
			}
			if (bFromInterface)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Use remove_interface for interface funcs; cannot delete graph alone"));
	return;
			}

			UEdGraph* Found = nullptr;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == FuncName) { Found = G; break; }
			}
			if (!Found) { Entry->SetStringField(TEXT("error"), TEXT("Function graph not found"));
	return; }
			FBlueprintEditorUtils::RemoveGraph(BP, Found, EGraphRemoveFlags::Recompile);
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}

	if (Action == TEXT("add_macro") || Action == TEXT("add_timeline") || Action == TEXT("add_dispatcher") || Action == TEXT("add_local_variable"))
	{
		if (Action == TEXT("add_macro"))
		{
			const FString MacroName = FNexusArgs(Op).Str(TEXT("functionName"));
			if (MacroName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_macro requires functionName"));
	return; }
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(BP, FName(*MacroName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (!NewGraph) { Entry->SetStringField(TEXT("error"), TEXT("Failed to create macro graph"));
	return; }
			FBlueprintEditorUtils::AddMacroGraph(BP, NewGraph, true, nullptr);
			Entry->SetStringField(TEXT("functionName"), MacroName);
		}
		else if (Action == TEXT("add_timeline"))
		{
			const FString TlName = FNexusArgs(Op).Str(TEXT("functionName"));
			if (TlName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_timeline requires functionName"));
	return; }
			UTimelineTemplate* Tl = FBlueprintEditorUtils::AddNewTimeline(BP, FName(*TlName));
			if (!Tl) { Entry->SetStringField(TEXT("error"), TEXT("Create Timeline failed"));
	return; }
			Entry->SetStringField(TEXT("functionName"), TlName);
		}
		else if (Action == TEXT("add_dispatcher"))
		{
			const FString VarName = FNexusArgs(Op).Str(TEXT("variableName"));
			if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_dispatcher requires variableName"));
	return; }
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
			FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
			Entry->SetStringField(TEXT("variableName"), VarName);
		}
		else
		{
			const FString FuncName = FNexusArgs(Op).Str(TEXT("functionName"));
			const FString VarName = FNexusArgs(Op).Str(TEXT("variableName"));
			const FString VarTypeRaw = Op->HasField(TEXT("variableType")) ? Op->GetStringField(TEXT("variableType")) : TEXT("bool");
			if (FuncName.IsEmpty() || VarName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_local_variable requires functionName and variableName"));
	return; }
			UEdGraph* FuncGraph = nullptr;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == FuncName) { FuncGraph = G; break; }
			}
			if (!FuncGraph) { Entry->SetStringField(TEXT("error"), TEXT("Function graph not found"));
	return; }
			FEdGraphPinType PinType;
			FString TypeErr;
			if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { Entry->SetStringField(TEXT("error"), TypeErr);
	return; }
			FBlueprintEditorUtils::AddLocalVariable(BP, FuncGraph, FName(*VarName), PinType);
			Entry->SetStringField(TEXT("variableName"), VarName);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}

static void HandleBP_Interface(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("add_interface") || Action == TEXT("remove_interface"))
	{
		const FString IfaceName = FNexusArgs(Op).Str(TEXT("interfaceName"));
		if (IfaceName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("interfaceName required (BPI path or interface class)"));
	return; }
		Entry->SetStringField(TEXT("interfaceName"), IfaceName);

		if (BP->BlueprintType == BPTYPE_Interface)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Use create parentClass for interface BPs; cannot add_interface"));
	return;
		}

		UClass* IfaceClass = ResolveInterfaceClass(IfaceName);
		if (!IfaceClass)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Interface not found or not Interface: %s (BPI path or GeneratedClass)"), *IfaceName));
	return;
		}
		if (IfaceClass->GetName() == TEXT("Interface"))
		{
			Entry->SetStringField(TEXT("error"), TEXT("Cannot implement native UInterface; pass BPI asset"));
	return;
		}
		Entry->SetStringField(TEXT("interfaceClass"), IfaceClass->GetName());

		if (Action == TEXT("add_interface"))
		{
			if (BlueprintAlreadyImplements(BP, IfaceClass))
			{
				Entry->SetStringField(TEXT("error"), TEXT("Interface already implemented"));
	return;
			}
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
			FBlueprintEditorUtils::ImplementNewInterface(BP, IfaceClass->GetClassPathName());
#else
			FBlueprintEditorUtils::ImplementNewInterface(BP, IfaceClass->GetFName());
#endif
			if (!BlueprintAlreadyImplements(BP, IfaceClass))
			{
				Entry->SetStringField(TEXT("error"), TEXT("ImplementNewInterface failed (requires compiled interface BP)"));
	return;
			}
		}
		else
		{
			if (!BlueprintAlreadyImplements(BP, IfaceClass))
			{
				Entry->SetStringField(TEXT("error"), TEXT("Interface not implemented"));
	return;
			}
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
			FBlueprintEditorUtils::RemoveInterface(BP, IfaceClass->GetClassPathName());
#else
			FBlueprintEditorUtils::RemoveInterface(BP, IfaceClass->GetFName());
#endif
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}

static void HandleBP_SCS(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;

	// set_defaults 写 CDO（ImportText），与 SCS 组件树无关；不受下方 Actor 限制，
	// 任意 Blueprint（含 GameplayAbility/UI/BPI）均可写自身 CDO 属性
	if (Action == TEXT("set_defaults"))
	{
		FString PropPath, Value;
		Op->TryGetStringField(TEXT("propertyPath"), PropPath);
		Op->TryGetStringField(TEXT("value"),        Value);
		if (PropPath.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set_defaults requires propertyPath"));
	return; }
		if (!BP->GeneratedClass) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint has no generated class"));
	return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO) { Entry->SetStringField(TEXT("error"), TEXT("Failed to get CDO"));
	return; }

		TArray<FString> Segments;
		PropPath.ParseIntoArray(Segments, TEXT("."), true);

		FProperty* Prop   = nullptr;
		void*      ValPtr = nullptr;
		FString    PropErr;
		if (!FNexusPropertyUtils::ResolvePropertyWrite(CDO, Segments, 0, Prop, ValPtr, PropErr)) { Entry->SetStringField(TEXT("error"), PropErr);
	return; }
		if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, CDO))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to set '%s' = '%s'"), *PropPath, *Value));
	return;
		}

		Entry->SetStringField(TEXT("propertyPath"), PropPath);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}

	if (Action == TEXT("add_component") || Action == TEXT("remove_component") ||
	    Action == TEXT("set_component_property"))
	{
		if (!BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()))
		{
			const FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("(none)");
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Blueprint parent is not Actor subclass: %s (parent=%s). add_component/remove_component/set_component_property need Actor BP; use add_variable/add_function/add_node for GA/UI/BPI, or manage_asset_gameplay_ability for GAS semantic fields."),
				*Ctx.AssetPath, *ParentName));
	return;
		}

		if (Action == TEXT("add_component"))
		{
			FString ComponentClassName, ComponentName;
			Op->TryGetStringField(TEXT("componentClass"), ComponentClassName);
			Op->TryGetStringField(TEXT("componentName"),  ComponentName);
			if (ComponentClassName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_component requires componentClass"));
	return; }
			if (ComponentName.IsEmpty())      { Entry->SetStringField(TEXT("error"), TEXT("add_component requires componentName"));
	return; }

			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint has no SimpleConstructionScript"));
	return; }

			UClass* CompClass = FNexusAssetUtils::FindClassWithUPrefix(ComponentClassName);
			if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Component class not found or not ActorComponent: %s"), *ComponentClassName));
	return;
			}
			if (SCS->FindSCSNode(*ComponentName))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Component '%s' already exists"), *ComponentName));
	return;
			}

			USCS_Node* NewNode = SCS->CreateNode(CompClass, *ComponentName);
			if (!NewNode) { Entry->SetStringField(TEXT("error"), TEXT("Create SCS nodefailed"));
	return; }

			FString AttachTo;
			Op->TryGetStringField(TEXT("attachTo"), AttachTo);
			if (!AttachTo.IsEmpty())
			{
				USCS_Node* ParentNode = SCS->FindSCSNode(*AttachTo);
				if (!ParentNode) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("attachTo component '%s' not found"), *AttachTo));
	return; }
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
	return;
		}

		if (Action == TEXT("remove_component"))
		{
			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint has no SimpleConstructionScript"));
	return; }

			FString ComponentName;
			Op->TryGetStringField(TEXT("componentName"), ComponentName);
			if (ComponentName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("remove_component requires componentName"));
	return; }

			USCS_Node* Node = SCS->FindSCSNode(*ComponentName);
			if (!Node) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Component '%s' not found"), *ComponentName));
	return; }

			Entry->SetStringField(TEXT("componentName"), ComponentName);
			SCS->RemoveNodeAndPromoteChildren(Node);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
	return;
		}

		if (Action == TEXT("set_component_property"))
		{
			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS) { Entry->SetStringField(TEXT("error"), TEXT("Blueprint has no SimpleConstructionScript"));
	return; }

			FString ComponentName, PropPath, Value;
			Op->TryGetStringField(TEXT("componentName"), ComponentName);
			Op->TryGetStringField(TEXT("propertyPath"),  PropPath);
			Op->TryGetStringField(TEXT("value"),         Value);
			if (ComponentName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("Missing componentName"));
	return; }
			if (PropPath.IsEmpty())      { Entry->SetStringField(TEXT("error"), TEXT("Missing propertyPath"));
	return; }

			USCS_Node* SCSNode = SCS->FindSCSNode(*ComponentName);
			if (!SCSNode || !SCSNode->ComponentTemplate)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Component '%s' not found"), *ComponentName));
	return;
			}

			UActorComponent* Template = SCSNode->ComponentTemplate;
			TArray<FString> Segments;
			PropPath.ParseIntoArray(Segments, TEXT("."), true);

			FProperty* Prop   = nullptr;
			void*      ValPtr = nullptr;
			FString    PropErr;
			if (!FNexusPropertyUtils::ResolvePropertyWrite(Template, Segments, 0, Prop, ValPtr, PropErr)) { Entry->SetStringField(TEXT("error"), PropErr);
	return; }
			if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, Template))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to set '%s' = '%s'"), *PropPath, *Value));
	return;
			}

			Template->MarkPackageDirty();
			Entry->SetStringField(TEXT("componentName"), ComponentName);
			Entry->SetStringField(TEXT("propertyPath"),  PropPath);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
	return;
		}
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}


bool FManageAssetBlueprintCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);

	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
	if (!BP)
	{
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		return false;
	}
	OutTarget = BP;
	return true;
#else
	OutError = TEXT("manage_asset_blueprint only available in editor builds");
	return false;
#endif
}

void FManageAssetBlueprintCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_variable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("remove_variable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("add_function"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("remove_function"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("add_macro"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("add_timeline"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("add_dispatcher"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("add_local_variable"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("add_interface"), &HandleBP_Interface);
	OutHandlers.Add(TEXT("remove_interface"), &HandleBP_Interface);
	OutHandlers.Add(TEXT("add_component"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("remove_component"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("set_component_property"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("set_defaults"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("add_node"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("remove_node"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("set_node"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("promote_pin"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("connect"), &FNexusBlueprintGraphOps::HandleWire);
	OutHandlers.Add(TEXT("disconnect"), &FNexusBlueprintGraphOps::HandleWire);
	OutHandlers.Add(TEXT("disconnect_all"), &FNexusBlueprintGraphOps::HandleWire);
}


REGISTER_MCP_CAPABILITY(FManageAssetBlueprintCapability)
