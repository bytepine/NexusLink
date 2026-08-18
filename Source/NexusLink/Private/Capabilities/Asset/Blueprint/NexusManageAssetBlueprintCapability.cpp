// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusManageAssetBlueprintCapability.h"
#include "NexusActionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPinTypeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
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
		const FString VarName = Op->HasField(TEXT("variableName")) ? Op->GetStringField(TEXT("variableName")) : TEXT("");
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
			if (!Op->HasField(TEXT("variableType"))) { Entry->SetStringField(TEXT("error"), TEXT("add_variable requires variableType"));
	return; }
			const FString VarTypeRaw = Op->GetStringField(TEXT("variableType"));

			bool bVarExists = false;
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{ if (Var.VarName.ToString() == VarName) { bVarExists = true; break; } }
			if (bVarExists) { Entry->SetStringField(TEXT("error"), TEXT("Variable already exists"));
	return; }

			FEdGraphPinType PinType;
			FString TypeErr;
			if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { Entry->SetStringField(TEXT("error"), TypeErr);
	return; }

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
		FKismetEditorUtilities::CompileBlueprint(BP);
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
		const FString FuncName = Op->HasField(TEXT("functionName")) ? Op->GetStringField(TEXT("functionName")) : TEXT("");
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
			const FString MacroName = Op->HasField(TEXT("functionName")) ? Op->GetStringField(TEXT("functionName")) : TEXT("");
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
			const FString TlName = Op->HasField(TEXT("functionName")) ? Op->GetStringField(TEXT("functionName")) : TEXT("");
			if (TlName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_timeline requires functionName"));
	return; }
			UTimelineTemplate* Tl = FBlueprintEditorUtils::AddNewTimeline(BP, FName(*TlName));
			if (!Tl) { Entry->SetStringField(TEXT("error"), TEXT("Create Timeline failed"));
	return; }
			Entry->SetStringField(TEXT("functionName"), TlName);
		}
		else if (Action == TEXT("add_dispatcher"))
		{
			const FString VarName = Op->HasField(TEXT("variableName")) ? Op->GetStringField(TEXT("variableName")) : TEXT("");
			if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("add_dispatcher requires variableName"));
	return; }
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
			FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
			Entry->SetStringField(TEXT("variableName"), VarName);
		}
		else
		{
			const FString FuncName = Op->HasField(TEXT("functionName")) ? Op->GetStringField(TEXT("functionName")) : TEXT("");
			const FString VarName = Op->HasField(TEXT("variableName")) ? Op->GetStringField(TEXT("variableName")) : TEXT("");
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
		const FString IfaceName = Op->HasField(TEXT("interfaceName")) ? Op->GetStringField(TEXT("interfaceName")) : TEXT("");
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
	if (Action == TEXT("add_component") || Action == TEXT("remove_component") ||
	    Action == TEXT("set_component_property") || Action == TEXT("set_defaults"))
	{
		if (!BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()))
		{
			const FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("(none)");
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Blueprint parent is not Actor subclass: %s (parent=%s). add_component/set_defaults need Actor BP; use add_variable/add_function/add_node for GA/UI/BPI."),
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

		// set_defaults
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
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("manage_asset_blueprint only available in editor builds"));
#endif
}

static void HandleBP_GraphNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	// ── Graph / Wire actions: require graphName ───────────────────────────────
	const FString GraphName = Op->HasField(TEXT("graphName")) ? Op->GetStringField(TEXT("graphName")) : TEXT("");
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
		if (!Op->HasField(TEXT("nodeClass"))) { Entry->SetStringField(TEXT("error"), TEXT("add_node requires nodeClass"));
	return; }
		const FString NodeClass = Op->GetStringField(TEXT("nodeClass"));
		const int32 PosX = Op->HasField(TEXT("posX")) ? static_cast<int32>(Op->GetNumberField(TEXT("posX"))) : 0;
		const int32 PosY = Op->HasField(TEXT("posY")) ? static_cast<int32>(Op->GetNumberField(TEXT("posY"))) : 0;

		UEdGraphNode* NewNode = nullptr;

		if (NodeClass == TEXT("K2Node_CallFunction"))
		{
			if (!Op->HasField(TEXT("functionName"))) { Entry->SetStringField(TEXT("error"), TEXT("K2Node_CallFunction requires functionName"));
	return; }
			const FString FuncName = Op->GetStringField(TEXT("functionName"));
			FString FuncClassName;
			Op->TryGetStringField(TEXT("functionClass"), FuncClassName);

			UFunction* Func = nullptr;
			if (!FuncClassName.IsEmpty())
			{
				if (UClass* Owner = FNexusAssetUtils::FindClassWithUPrefix(FuncClassName))
					Func = Owner->FindFunctionByName(*FuncName);
			}
			if (!Func)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (UFunction* F = It->FindFunctionByName(*FuncName))
					{ Func = F; break; }
				}
			}
			if (!Func)
			{
				const FString K2Name = TEXT("K2_") + FuncName;
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (UFunction* F = It->FindFunctionByName(*K2Name))
					{ Func = F; break; }
				}
			}
			if (!Func) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Function '%s' not found (tried 'K2_%s')"), *FuncName, *FuncName));
	return; }

			UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
			Node->SetFlags(RF_Transactional);
			Node->SetFromFunction(Func);
			Graph->AddNode(Node, false, false);
			Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
			Node->NodePosX = PosX; Node->NodePosY = PosY;
			NewNode = Node;
		}
		else if (NodeClass == TEXT("K2Node_Event"))
		{
			// functionName：ReceiveBeginPlay / BeginPlay；functionClass 默认 Actor
			if (!Op->HasField(TEXT("functionName"))) { Entry->SetStringField(TEXT("error"), TEXT("K2Node_Event requires functionName (e.g. ReceiveBeginPlay)"));
	return; }
			FString EventName = Op->GetStringField(TEXT("functionName"));
			if (EventName == TEXT("BeginPlay")) EventName = TEXT("ReceiveBeginPlay");
			FString EventClassName = TEXT("Actor");
			Op->TryGetStringField(TEXT("functionClass"), EventClassName);
			UClass* EventClass = FNexusAssetUtils::FindClassWithUPrefix(EventClassName);
			if (!EventClass) EventClass = AActor::StaticClass();
			if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(BP, EventClass, FName(*EventName)))
			{
				Existing->SetEnabledState(ENodeEnabledState::Enabled, true);
				NewNode = Existing;
			}
			else
			{
				UK2Node_Event* Node = NewObject<UK2Node_Event>(Graph);
				Node->SetFlags(RF_Transactional);
				Node->EventReference.SetExternalMember(FName(*EventName), EventClass);
				Node->bOverrideFunction = true;
				Graph->AddNode(Node, false, false);
				Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
				Node->NodePosX = PosX; Node->NodePosY = PosY;
				NewNode = Node;
			}
		}
		else if (NodeClass == TEXT("K2Node_VariableGet") || NodeClass == TEXT("K2Node_VariableSet"))
		{
			if (!Op->HasField(TEXT("variableName"))) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("%s requires variableName"), *NodeClass));
	return; }
			const FString VarName = Op->GetStringField(TEXT("variableName"));

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
			{ Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Node class '%s' not found"), *NodeClass));
	return; }
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
	return;
	}

	if (Action == TEXT("remove_node"))
	{
		if (!Op->HasField(TEXT("nodeId"))) { Entry->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId"));
	return; }
		const FString NodeIdStr = Op->GetStringField(TEXT("nodeId"));
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
		if (!Op->HasField(TEXT("nodeId"))) { Entry->SetStringField(TEXT("error"), TEXT("set_node requires nodeId"));
	return; }
		const FString NodeIdStr = Op->GetStringField(TEXT("nodeId"));
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

		UEdGraphNode* Spawned = NodeInfo.PerformAction(Graph, TargetPin, NewNodePos, false);
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

static void HandleBP_Wire(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;

	const FString GraphName = Op->HasField(TEXT("graphName")) ? Op->GetStringField(TEXT("graphName")) : TEXT("");
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

	const FString SrcNodeId  = Op->HasField(TEXT("sourceNodeId"))  ? Op->GetStringField(TEXT("sourceNodeId"))  : TEXT("");
	const FString SrcPinName = Op->HasField(TEXT("sourcePinName")) ? Op->GetStringField(TEXT("sourcePinName")) : TEXT("");
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
		if (!Op->HasField(TEXT("targetNodeId")) || !Op->HasField(TEXT("targetPinName")))
		{ Entry->SetStringField(TEXT("error"), TEXT("connect requires targetNodeId and targetPinName"));
	return;}
		const FString DstNodeId  = Op->GetStringField(TEXT("targetNodeId"));
		const FString DstPinName = Op->GetStringField(TEXT("targetPinName"));
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
		if (!Op->HasField(TEXT("targetNodeId")) || !Op->HasField(TEXT("targetPinName")))
		{ Entry->SetStringField(TEXT("error"), TEXT("disconnect requires targetNodeId and targetPinName"));
	return;}
		UEdGraphNode* DstNode = FNexusBlueprintGraphUtils::FindBPNode(Graph, Op->GetStringField(TEXT("targetNodeId")));
		if (!DstNode) { Entry->SetStringField(TEXT("error"), TEXT("Target node not found"));
	return;}
		const FString DstPinName = Op->GetStringField(TEXT("targetPinName"));
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
	OutHandlers.Add(TEXT("add_node"), &HandleBP_GraphNode);
	OutHandlers.Add(TEXT("remove_node"), &HandleBP_GraphNode);
	OutHandlers.Add(TEXT("set_node"), &HandleBP_GraphNode);
	OutHandlers.Add(TEXT("promote_pin"), &HandleBP_GraphNode);
	OutHandlers.Add(TEXT("connect"), &HandleBP_Wire);
	OutHandlers.Add(TEXT("disconnect"), &HandleBP_Wire);
	OutHandlers.Add(TEXT("disconnect_all"), &HandleBP_Wire);
}


REGISTER_MCP_CAPABILITY(FManageAssetBlueprintCapability)
