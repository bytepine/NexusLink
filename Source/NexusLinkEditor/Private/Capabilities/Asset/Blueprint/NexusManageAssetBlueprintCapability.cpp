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
#include "Utils/NexusAssetEditUtils.h"
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


void FNexusManageAssetBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetBlueprint");
	Out.bInjectCompile = true;
	Out.SearchAssetTypes = {TEXT("Blueprint")};
	Out.Description = TEXT("Batch edit BP: graphs/vars/funcs/macros/timelines/dispatchers/interfaces/nodes/PromotePin.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),          FNexusSchema::Enum(TEXT("Operation type"), {
			TEXT("AddVariable"), TEXT("RemoveVariable"),
			TEXT("AddFunction"), TEXT("RemoveFunction"),
			TEXT("AddMacro"), TEXT("AddTimeline"), TEXT("AddDispatcher"), TEXT("AddLocalVariable"),
			TEXT("AddInterface"), TEXT("RemoveInterface"),
			TEXT("AddNode"), TEXT("RemoveNode"), TEXT("SetNode"), TEXT("PromotePin"),
			TEXT("Connect"), TEXT("Disconnect"), TEXT("DisconnectAll"),
			TEXT("AddComponent"), TEXT("RemoveComponent"), TEXT("SetComponentProperty"), TEXT("SetDefaults")
		}))
		.Prop(TEXT("GraphName"),       FNexusSchema::Str(TEXT("Graph name (nodes/wires/PromotePin)")))
		.Prop(TEXT("VariableName"),    FNexusSchema::Str(TEXT("Variable name (AddVariable/PromotePin; auto if omitted)")))
		.Prop(TEXT("VariableType"),    FNexusSchema::Str(TEXT("Basic or object type (AddVariable)")))
		.Prop(TEXT("DefaultValue"),    FNexusSchema::Str(TEXT("Default value (AddVariable)")))
		.Prop(TEXT("Category"),        FNexusSchema::Str(TEXT("Editor category (AddVariable)")))
		.Prop(TEXT("bIsPublic"),        FNexusSchema::Bool(TEXT("Instance editable (AddVariable)"), true, false))
		.Prop(TEXT("bIsLocal"),         FNexusSchema::Bool(TEXT("PromotePin: local var (default member var)"), true, false))
		.Prop(TEXT("NodeId"),          FNexusSchema::Str(TEXT("node GUID (remove/SetNode/PromotePin)")))
		.Prop(TEXT("NodeClass"),       FNexusSchema::Str(TEXT("K2Node class (AddNode)")))
		.Prop(TEXT("FunctionName"),    FNexusSchema::Str(TEXT("Function name (AddFunction/CallFunction)")))
		.Prop(TEXT("FunctionClass"),   FNexusSchema::Str(TEXT("CallFunction: owner class")))
		.Prop(TEXT("InterfaceName"),   FNexusSchema::Str(TEXT("Interface class or BPI path (add/RemoveInterface)")))
		.Prop(TEXT("PosX"),            FNexusSchema::Num(TEXT("Node X position")))
		.Prop(TEXT("PosY"),            FNexusSchema::Num(TEXT("Node Y position")))
		.Prop(TEXT("Comment"),         FNexusSchema::Str(TEXT("Node comment (SetNode)")))
		.Prop(TEXT("PinName"),         FNexusSchema::Str(TEXT("Pin name (SetNode/PromotePin)")))
		.Prop(TEXT("PinDefaultValue"), FNexusSchema::Str(TEXT("New pin default value")))
		.Prop(TEXT("SourceNodeId"),    FNexusSchema::Str(TEXT("Source node GUID (wire ops)")))
		.Prop(TEXT("SourcePinName"),   FNexusSchema::Str(TEXT("Source pin name")))
		.Prop(TEXT("TargetNodeId"),    FNexusSchema::Str(TEXT("Target node GUID (connect/disconnect)")))
		.Prop(TEXT("TargetPinName"),   FNexusSchema::Str(TEXT("Target pin name")))
		.Prop(TEXT("ComponentClass"),  FNexusSchema::Str(TEXT("Component class (AddComponent), e.g. StaticMeshComponent")))
		.Prop(TEXT("ComponentName"),   FNexusSchema::Str(TEXT("SCS variable name (add/remove/SetComponentProperty)")))
		.Prop(TEXT("AttachTo"),        FNexusSchema::Str(TEXT("Parent component var (AddComponent); default scene root if omitted")))
		.Prop(TEXT("PropertyPath"),    FNexusSchema::Str(TEXT("Property path, dot notation e.g. RelativeLocation.X")))
		.Prop(TEXT("Value"),           FNexusSchema::Str(TEXT("String value e.g. (X=100,Y=0,Z=50) or true")))
		.Prop(TEXT("ExpectedValue"), FNexusSchema::Str(TEXT("Expected previous scalar value (SetDefaults only)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Blueprint asset path")))
		.Prop(TEXT("bVerify"), FNexusSchema::Bool(TEXT("Verify scalar SetDefaults after compilation before saving; rejects other actions"), true, false))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("variable"), TEXT("Node"), TEXT("Component"), TEXT("wire"), TEXT("Connect"),
		TEXT("Disconnect"), TEXT("link"), TEXT("scs"), TEXT("Function"), TEXT("interface"), TEXT("bpi"),
		TEXT("promote"), TEXT("pin")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetBlueprint"), TEXT("CreateAssetBlueprint"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Write ops: add/remove vars, PromotePin, func graphs, interfaces, nodes, wires");
}

static void HandleBP_Variable(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("AddVariable") || Action == TEXT("RemoveVariable"))
	{
		const FString VarName = FNexusArgs(Op).Str(TEXT("VariableName"));
		if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("VariableName is required"));
	return; }
		Entry->SetStringField(TEXT("VariableName"), VarName);

		if (Action == TEXT("RemoveVariable"))
		{
			const FName VarFName(*VarName);
			bool bFound = false;
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{ if (Var.VarName == VarFName) { bFound = true; break; } }
			if (!bFound) { Entry->SetStringField(TEXT("Error"), TEXT("Variable not found (or inherited)"));
	return; }
			FBlueprintEditorUtils::RemoveMemberVariable(BP, VarFName);
		}
		else
		{
			const FString VarTypeRaw = FNexusArgs(Op).Str(TEXT("VariableType"));
			if (VarTypeRaw.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("AddVariable requires VariableType"));
	return; }

			bool bVarExists = false;
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{ if (Var.VarName.ToString() == VarName) { bVarExists = true; break; } }
			if (bVarExists) { Entry->SetStringField(TEXT("Error"), TEXT("Variable already exists"));
	return; }

			FEdGraphPinType PinType;
			FString TypeErr;
			if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { Entry->SetStringField(TEXT("Error"), TypeErr);
	return; }

			BP->SetFlags(RF_Transactional);
			FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);

			if (Op->HasField(TEXT("DefaultValue")))
			{
				const FString DefaultVal = Op->GetStringField(TEXT("DefaultValue"));
				for (FBPVariableDescription& Var : BP->NewVariables)
				{ if (Var.VarName.ToString() == VarName) { Var.DefaultValue = DefaultVal; break; } }
			}
			if (Op->HasField(TEXT("Category")))
			{
				FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, FName(*VarName), nullptr,
					FText::FromString(Op->GetStringField(TEXT("Category"))));
			}
			bool bIsPublic = false;
			if (Op->HasField(TEXT("bIsPublic"))) bIsPublic = Op->GetBoolField(TEXT("bIsPublic"));
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
			Entry->SetStringField(TEXT("VariableType"), VarTypeRaw.ToLower());
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		// 全量 Compile 会 CollectGarbage，冲掉进行中的 Undo 快照；bCompile=true 走 ApplyIfRequested（事务外）
		if (!FNexusEditorTransaction::IsTransactionActive())
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
		}
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ManageAssetBlueprint only available in editor builds"));
#endif
}

static void HandleBP_FunctionGraph(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("AddFunction") || Action == TEXT("RemoveFunction"))
	{
		const FString FuncName = FNexusArgs(Op).Str(TEXT("FunctionName"));
		if (FuncName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("FunctionName is required"));
	return; }
		Entry->SetStringField(TEXT("FunctionName"), FuncName);

		if (Action == TEXT("AddFunction"))
		{
			bool bExists = false;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == FuncName) { bExists = true; break; }
			}
			if (bExists) { Entry->SetStringField(TEXT("Error"), TEXT("Function graph already exists"));
	return; }

			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*FuncName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (!NewGraph) { Entry->SetStringField(TEXT("Error"), TEXT("Failed to create function graph"));
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
				Entry->SetStringField(TEXT("Error"), TEXT("Use RemoveInterface for interface funcs; cannot delete graph alone"));
	return;
			}

			UEdGraph* Found = nullptr;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == FuncName) { Found = G; break; }
			}
			if (!Found) { Entry->SetStringField(TEXT("Error"), TEXT("Function graph not found"));
	return; }
			FBlueprintEditorUtils::RemoveGraph(BP, Found, EGraphRemoveFlags::Recompile);
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}

	if (Action == TEXT("AddMacro") || Action == TEXT("AddTimeline") || Action == TEXT("AddDispatcher") || Action == TEXT("AddLocalVariable"))
	{
		if (Action == TEXT("AddMacro"))
		{
			const FString MacroName = FNexusArgs(Op).Str(TEXT("FunctionName"));
			if (MacroName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("AddMacro requires FunctionName"));
	return; }
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(BP, FName(*MacroName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (!NewGraph) { Entry->SetStringField(TEXT("Error"), TEXT("Failed to create macro graph"));
	return; }
			FBlueprintEditorUtils::AddMacroGraph(BP, NewGraph, true, nullptr);
			Entry->SetStringField(TEXT("FunctionName"), MacroName);
		}
		else if (Action == TEXT("AddTimeline"))
		{
			const FString TlName = FNexusArgs(Op).Str(TEXT("FunctionName"));
			if (TlName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("AddTimeline requires FunctionName"));
	return; }
			UTimelineTemplate* Tl = FBlueprintEditorUtils::AddNewTimeline(BP, FName(*TlName));
			if (!Tl) { Entry->SetStringField(TEXT("Error"), TEXT("Create Timeline failed"));
	return; }
			Entry->SetStringField(TEXT("FunctionName"), TlName);
		}
		else if (Action == TEXT("AddDispatcher"))
		{
			const FString VarName = FNexusArgs(Op).Str(TEXT("VariableName"));
			if (VarName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("AddDispatcher requires VariableName"));
	return; }
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
			FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
			Entry->SetStringField(TEXT("VariableName"), VarName);
		}
		else
		{
			const FString FuncName = FNexusArgs(Op).Str(TEXT("FunctionName"));
			const FString VarName = FNexusArgs(Op).Str(TEXT("VariableName"));
			const FString VarTypeRaw = Op->HasField(TEXT("VariableType")) ? Op->GetStringField(TEXT("VariableType")) : TEXT("bool");
			if (FuncName.IsEmpty() || VarName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("AddLocalVariable requires FunctionName and VariableName"));
	return; }
			UEdGraph* FuncGraph = nullptr;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == FuncName) { FuncGraph = G; break; }
			}
			if (!FuncGraph) { Entry->SetStringField(TEXT("Error"), TEXT("Function graph not found"));
	return; }
			FEdGraphPinType PinType;
			FString TypeErr;
			if (!FNexusPinTypeUtils::ParsePinType(VarTypeRaw, PinType, TypeErr)) { Entry->SetStringField(TEXT("Error"), TypeErr);
	return; }
			FBlueprintEditorUtils::AddLocalVariable(BP, FuncGraph, FName(*VarName), PinType);
			Entry->SetStringField(TEXT("VariableName"), VarName);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ManageAssetBlueprint only available in editor builds"));
#endif
}

static void HandleBP_Interface(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("AddInterface") || Action == TEXT("RemoveInterface"))
	{
		const FString IfaceName = FNexusArgs(Op).Str(TEXT("InterfaceName"));
		if (IfaceName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("InterfaceName required (BPI path or interface class)"));
	return; }
		Entry->SetStringField(TEXT("InterfaceName"), IfaceName);

		if (BP->BlueprintType == BPTYPE_Interface)
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Use create ParentClass for interface BPs; cannot AddInterface"));
	return;
		}

		UClass* IfaceClass = ResolveInterfaceClass(IfaceName);
		if (!IfaceClass)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("Interface not found or not Interface: %s (BPI path or GeneratedClass)"), *IfaceName));
	return;
		}
		if (IfaceClass->GetName() == TEXT("Interface"))
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Cannot implement native UInterface; pass BPI asset"));
	return;
		}
		Entry->SetStringField(TEXT("InterfaceClass"), IfaceClass->GetName());

		if (Action == TEXT("AddInterface"))
		{
			if (BlueprintAlreadyImplements(BP, IfaceClass))
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Interface already implemented"));
	return;
			}
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
			FBlueprintEditorUtils::ImplementNewInterface(BP, IfaceClass->GetClassPathName());
#else
			FBlueprintEditorUtils::ImplementNewInterface(BP, IfaceClass->GetFName());
#endif
			if (!BlueprintAlreadyImplements(BP, IfaceClass))
			{
				Entry->SetStringField(TEXT("Error"), TEXT("ImplementNewInterface failed (requires compiled interface BP)"));
	return;
			}
		}
		else
		{
			if (!BlueprintAlreadyImplements(BP, IfaceClass))
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Interface not implemented"));
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
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ManageAssetBlueprint only available in editor builds"));
#endif
}

static void HandleBP_SCS(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;

	// SetDefaults 写 CDO（ImportText），与 SCS 组件树无关；不受下方 Actor 限制，
	// 任意 Blueprint（含 GameplayAbility/UI/BPI）均可写自身 CDO 属性
	if (Action == TEXT("SetDefaults"))
	{
		if (FNexusAssetEditUtils::IsVerified(Ctx.Args) || Op->HasField(TEXT("ExpectedValue")))
		{
			FNexusAssetEditUtils::Apply(BP, TEXT("ManageAssetBlueprint"), Op, FNexusAssetEditUtils::IsVerified(Ctx.Args), Entry);
			return;
		}
		FString PropPath, Value;
		Op->TryGetStringField(TEXT("PropertyPath"), PropPath);
		Op->TryGetStringField(TEXT("Value"),        Value);
		if (PropPath.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("SetDefaults requires PropertyPath"));
	return; }
		if (!BP->GeneratedClass) { Entry->SetStringField(TEXT("Error"), TEXT("Blueprint has no generated class"));
	return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO) { Entry->SetStringField(TEXT("Error"), TEXT("Failed to get CDO"));
	return; }

		TArray<FString> Segments;
		PropPath.ParseIntoArray(Segments, TEXT("."), true);

		FProperty* Prop   = nullptr;
		void*      ValPtr = nullptr;
		FString    PropErr;
		if (!FNexusPropertyUtils::ResolvePropertyWrite(CDO, Segments, 0, Prop, ValPtr, PropErr)) { Entry->SetStringField(TEXT("Error"), PropErr);
	return; }
		FString OldValue;
		FNexusPropertyUtils::ExportText(Prop, OldValue, ValPtr);
		BP->Modify();
		CDO->SetFlags(RF_Transactional);
		CDO->Modify();
		if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, CDO))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to set '%s' = '%s'"), *PropPath, *Value));
	return;
		}

		Entry->SetStringField(TEXT("PropertyPath"), PropPath);
		FString ActualValue;
		FNexusPropertyUtils::ExportText(Prop, ActualValue, ValPtr);
		Entry->SetStringField(TEXT("OldValue"), OldValue);
		Entry->SetStringField(TEXT("NewValue"), ActualValue);
		Entry->SetStringField(TEXT("Verification"), TEXT("NotRequested"));
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	return;
	}

	if (Action == TEXT("AddComponent") || Action == TEXT("RemoveComponent") ||
	    Action == TEXT("SetComponentProperty"))
	{
		if (!BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()))
		{
			const FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("(none)");
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("Blueprint parent is not Actor subclass: %s (Parent=%s). AddComponent/RemoveComponent/SetComponentProperty need Actor BP; use AddVariable/AddFunction/AddNode for GA/UI/BPI, or ManageAssetGameplayAbility for GAS semantic fields."),
				*Ctx.AssetPath, *ParentName));
	return;
		}

		if (Action == TEXT("AddComponent"))
		{
			FString ComponentClassName, ComponentName;
			Op->TryGetStringField(TEXT("ComponentClass"), ComponentClassName);
			Op->TryGetStringField(TEXT("ComponentName"),  ComponentName);
			if (ComponentClassName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("AddComponent requires ComponentClass"));
	return; }
			if (ComponentName.IsEmpty())      { Entry->SetStringField(TEXT("Error"), TEXT("AddComponent requires ComponentName"));
	return; }

			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS) { Entry->SetStringField(TEXT("Error"), TEXT("Blueprint has no SimpleConstructionScript"));
	return; }

			UClass* CompClass = FNexusAssetUtils::FindClassWithUPrefix(ComponentClassName);
			if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Component class not found or not ActorComponent: %s"), *ComponentClassName));
	return;
			}
			if (SCS->FindSCSNode(*ComponentName))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Component '%s' already exists"), *ComponentName));
	return;
			}

			USCS_Node* NewNode = SCS->CreateNode(CompClass, *ComponentName);
			if (!NewNode) { Entry->SetStringField(TEXT("Error"), TEXT("Create SCS nodefailed"));
	return; }

			FString AttachTo;
			Op->TryGetStringField(TEXT("AttachTo"), AttachTo);
			if (!AttachTo.IsEmpty())
			{
				USCS_Node* ParentNode = SCS->FindSCSNode(*AttachTo);
				if (!ParentNode) { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("AttachTo component '%s' not found"), *AttachTo));
	return; }
				ParentNode->AddChildNode(NewNode);
			}
			else
			{
				USCS_Node* DefaultRoot = SCS->GetDefaultSceneRootNode();
				if (DefaultRoot) DefaultRoot->AddChildNode(NewNode);
				else             SCS->AddNode(NewNode);
			}

			Entry->SetStringField(TEXT("ComponentName"),  ComponentName);
			Entry->SetStringField(TEXT("ComponentClass"), CompClass->GetName());
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
	return;
		}

		if (Action == TEXT("RemoveComponent"))
		{
			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS) { Entry->SetStringField(TEXT("Error"), TEXT("Blueprint has no SimpleConstructionScript"));
	return; }

			FString ComponentName;
			Op->TryGetStringField(TEXT("ComponentName"), ComponentName);
			if (ComponentName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("RemoveComponent requires ComponentName"));
	return; }

			USCS_Node* Node = SCS->FindSCSNode(*ComponentName);
			if (!Node) { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Component '%s' not found"), *ComponentName));
	return; }

			Entry->SetStringField(TEXT("ComponentName"), ComponentName);
			SCS->RemoveNodeAndPromoteChildren(Node);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
	return;
		}

		if (Action == TEXT("SetComponentProperty"))
		{
			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS) { Entry->SetStringField(TEXT("Error"), TEXT("Blueprint has no SimpleConstructionScript"));
	return; }

			FString ComponentName, PropPath, Value;
			Op->TryGetStringField(TEXT("ComponentName"), ComponentName);
			Op->TryGetStringField(TEXT("PropertyPath"),  PropPath);
			Op->TryGetStringField(TEXT("Value"),         Value);
			if (ComponentName.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("Missing ComponentName"));
	return; }
			if (PropPath.IsEmpty())      { Entry->SetStringField(TEXT("Error"), TEXT("Missing PropertyPath"));
	return; }

			USCS_Node* SCSNode = SCS->FindSCSNode(*ComponentName);
			if (!SCSNode || !SCSNode->ComponentTemplate)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Component '%s' not found"), *ComponentName));
	return;
			}

			UActorComponent* Template = SCSNode->ComponentTemplate;
			TArray<FString> Segments;
			PropPath.ParseIntoArray(Segments, TEXT("."), true);

			FProperty* Prop   = nullptr;
			void*      ValPtr = nullptr;
			FString    PropErr;
			if (!FNexusPropertyUtils::ResolvePropertyWrite(Template, Segments, 0, Prop, ValPtr, PropErr)) { Entry->SetStringField(TEXT("Error"), PropErr);
	return; }
			if (!FNexusPropertyUtils::ImportTextFromString(Prop, Value, ValPtr, Template))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to set '%s' = '%s'"), *PropPath, *Value));
	return;
			}

			Template->MarkPackageDirty();
			Entry->SetStringField(TEXT("ComponentName"), ComponentName);
			Entry->SetStringField(TEXT("PropertyPath"),  PropPath);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
	return;
		}
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ManageAssetBlueprint only available in editor builds"));
#endif
}


bool FNexusManageAssetBlueprintCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	if (!FNexusAssetEditUtils::ValidateOperations(TEXT("ManageAssetBlueprint"), Args, OutError)) return false;
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);

	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
	if (!BP)
	{
		Entry->SetStringField(TEXT("Path"), AssetPath);
		Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		return false;
	}
	OutTarget = BP;
	return true;
#else
	OutError = TEXT("ManageAssetBlueprint only available in editor builds");
	return false;
#endif
}

void FNexusManageAssetBlueprintCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddVariable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("RemoveVariable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("AddFunction"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("RemoveFunction"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("AddMacro"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("AddTimeline"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("AddDispatcher"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("AddLocalVariable"), &HandleBP_FunctionGraph);
	OutHandlers.Add(TEXT("AddInterface"), &HandleBP_Interface);
	OutHandlers.Add(TEXT("RemoveInterface"), &HandleBP_Interface);
	OutHandlers.Add(TEXT("AddComponent"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("RemoveComponent"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("SetComponentProperty"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("SetDefaults"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("AddNode"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("RemoveNode"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("SetNode"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("PromotePin"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("Connect"), &FNexusBlueprintGraphOps::HandleWire);
	OutHandlers.Add(TEXT("Disconnect"), &FNexusBlueprintGraphOps::HandleWire);
	OutHandlers.Add(TEXT("DisconnectAll"), &FNexusBlueprintGraphOps::HandleWire);
}


REGISTER_MCP_CAPABILITY(FNexusManageAssetBlueprintCapability)
