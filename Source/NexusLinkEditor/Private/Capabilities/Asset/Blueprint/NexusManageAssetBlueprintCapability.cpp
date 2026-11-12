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
#include "Utils/NexusBlueprintEditUtils.h"
#include "K2Node_FunctionEntry.h"
#include "Utils/NexusAssetStructureUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "Utils/NexusBlueprintGraphEnhancements.h"
#include "Utils/NexusEditorTransaction.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#if WITH_EDITOR
#include "Editor.h"
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
#include "Kismet2/Kismet2NameValidators.h"
#include "KismetCompilerModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
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
	Out.Description = TEXT("Batch edit BP graphs, variables/types, inheritance, defaults and components. Compile/save explicitly.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),          FNexusSchema::Enum(TEXT("Operation type"), {
			TEXT("AddVariable"), TEXT("RemoveVariable"), TEXT("RenameVariable"), TEXT("RenameLocalVariable"), TEXT("ChangeVariableType"), TEXT("SetVariableMetadata"), TEXT("SetVariableReplication"), TEXT("SetFunctionCallInEditor"), TEXT("Reparent"),
			TEXT("AddFunction"), TEXT("RemoveFunction"),
			TEXT("AddMacro"), TEXT("AddTimeline"), TEXT("AddDispatcher"), TEXT("AddLocalVariable"),
			TEXT("AddInterface"), TEXT("RemoveInterface"),
			TEXT("AddNode"), TEXT("RemoveNode"), TEXT("SetNode"), TEXT("PromotePin"),
			TEXT("Connect"), TEXT("Disconnect"), TEXT("DisconnectAll"),
			TEXT("AddComponent"), TEXT("RemoveComponent"), TEXT("SetComponentProperty"), TEXT("ResetComponentPropertyToDefault"), TEXT("CopyComponent"), TEXT("ReattachComponent"), TEXT("SetDefaults"), TEXT("ArrangeNodes"), TEXT("WriteGraphDSL"), TEXT("WriteGraphPlan"), TEXT("AddFunctionParameter"), TEXT("RemoveFunctionParameter"), TEXT("SetVariableCategory"), TEXT("SetVariableEditable") }))
        .Prop(TEXT("Code"), FNexusSchema::Str(TEXT("UnrealGraphText node text from GetAssetBlueprint GraphDSL; append-only, 1-500 duplicable nodes; old Toolset Lisp syntax is not accepted")))
        .Prop(TEXT("GraphNodes"), FNexusSchema::ArrayOf(TEXT("Append-only plan nodes with Id and existing AddNode fields"), FNexusSchema::AnyObject(TEXT("Plan node"))))
        .Prop(TEXT("GraphLinks"), FNexusSchema::ArrayOf(TEXT("Plan links with From/FromPin/To/ToPin"), FNexusSchema::AnyObject(TEXT("Plan link"))))
        .Prop(TEXT("ParameterName"), FNexusSchema::Str(TEXT("Function parameter name")))
        .Prop(TEXT("bInput"), FNexusSchema::Bool(TEXT("Function input rather than output"), true, true))
        .Prop(TEXT("Category"), FNexusSchema::Str(TEXT("Blueprint variable category")))
        .Prop(TEXT("bEditable"), FNexusSchema::Bool(TEXT("Expose variable on instances")))
		.Prop(TEXT("ToolTip"), FNexusSchema::Str(TEXT("SetVariableMetadata: variable tooltip; empty clears it")))
		.Prop(TEXT("Replication"), FNexusSchema::Enum(TEXT("SetVariableReplication: does not enable replication on the owning actor/component"), {TEXT("None"), TEXT("Replicated"), TEXT("RepNotify")}))
		.Prop(TEXT("RepNotifyFunction"), FNexusSchema::Str(TEXT("RepNotify: existing owned function with no inputs/outputs; create with AddFunction first")))
		.Prop(TEXT("bCallInEditor"), FNexusSchema::Bool(TEXT("SetFunctionCallInEditor: expose an owned parameterless function as an editor button")))
		.Prop(TEXT("GraphName"),       FNexusSchema::Str(TEXT("Graph name (nodes/wires/PromotePin)")))
		.Prop(TEXT("VariableName"),    FNexusSchema::Str(TEXT("Variable name (AddVariable/PromotePin; auto if omitted)")))
		.Prop(TEXT("VariableType"),    FNexusSchema::Str(TEXT("Basic, object, ClassReference<BaseClass> or BlueprintType struct; T[], Array<T>, Set<T>, Map<K,V> (no nested containers)")))
		.Prop(TEXT("NewVariableName"), FNexusSchema::Str(TEXT("New member name (RenameVariable); leaf Blueprint only, preserves GUID and references; descendants/RepNotify are rejected")))
		.Prop(TEXT("ParentClass"), FNexusSchema::Str(TEXT("Reparent: new native class name or Blueprint/generated class path; normal, Widget, Anim and GameplayAbility Blueprints")))
		.Prop(TEXT("bAllowPotentialDataLoss"), FNexusSchema::Bool(TEXT("Reparent/ChangeVariableType: explicitly allow losing defaults or breaking graph references"), true, false))
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
		.Prop(TEXT("Baseline"), FNexusSchema::Enum(TEXT("Component reset baseline"), {TEXT("ParentTemplate"), TEXT("ClassDefault")}, TEXT("ParentTemplate")))
		.Prop(TEXT("ComponentName"),   FNexusSchema::Str(TEXT("SCS variable name or native component object/member name for property edits; native defaults also support SetDefaults with a member path such as Mesh.bHiddenInGame")))
		.Prop(TEXT("AttachTo"),        FNexusSchema::Str(TEXT("Parent component. Current SCS, inherited SCS, or a native component. AddComponent defaults to the scene root.")))
		.Prop(TEXT("SourceAssetPath"), FNexusSchema::Str(TEXT("Source Blueprint (CopyComponent)")))
		.Prop(TEXT("SourceComponentName"), FNexusSchema::Str(TEXT("Source SCS variable (CopyComponent)")))
		.Prop(TEXT("PropertyPath"),    FNexusSchema::Str(TEXT("Property path, dot notation e.g. RelativeLocation.X")))
		.Prop(TEXT("Value"),           FNexusSchema::Str(TEXT("String value e.g. (X=100,Y=0,Z=50) or true")))
		.Prop(TEXT("ExpectedValue"), FNexusSchema::Str(TEXT("Expected previous scalar value (SetDefaults only)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Blueprint asset path")))
		.Prop(TEXT("ExpectedStructureHash"), FNexusSchema::Str(TEXT("Precondition hash from GetAssetEditPreview")))
		.Prop(TEXT("bVerifyStructure"), FNexusSchema::Bool(TEXT("Verify RemoveNode/Disconnect/DisconnectAll against an isolated structural plan"), true, false))
		.Prop(TEXT("bVerify"), FNexusSchema::Bool(TEXT("Verify scalar SetDefaults/SetComponentProperty before saving; bCompile controls one final compilation; component resets may share the batch without ExpectedValue"), true, false))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("variable"), TEXT("rename"), TEXT("reparent"), TEXT("parent"), TEXT("Node"), TEXT("Component"), TEXT("wire"), TEXT("Connect"),
		TEXT("Disconnect"), TEXT("link"), TEXT("scs"), TEXT("Function"), TEXT("interface"), TEXT("bpi"),
		TEXT("promote"), TEXT("pin"), TEXT("metadata"), TEXT("tooltip"), TEXT("replication"), TEXT("replicated"), TEXT("repnotify"), TEXT("CallInEditor")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetBlueprint"), TEXT("CreateAssetBlueprint"), TEXT("SaveAsset"), TEXT("GetAssetEditPreview"), TEXT("CompileBlueprint"), TEXT("ControlEditorAssetEditor") };
	Out.WhenToUse = TEXT("Write Blueprint graphs/variables, reparent specialized Blueprints, or edit component defaults; preview first");
}

static void HandleBP_Reparent(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
    FString Error;
    const FNexusArgs Args(Op);
    if (!FNexusBlueprintEditUtils::Reparent(static_cast<UBlueprint*>(Ctx.Target), Args.Str(TEXT("ParentClass")), Args.Bool(TEXT("bAllowPotentialDataLoss")), Ctx.Entry, Error))
        Ctx.Entry->SetStringField(TEXT("Error"), Error);
}

static void HandleBP_Variable(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("SetFunctionCallInEditor"))
	{
		UEdGraph* Graph = FNexusBlueprintGraphUtils::FindBPGraph(BP, FNexusArgs(Op).Str(TEXT("FunctionName")));
		if (!Graph || !BP->FunctionGraphs.Contains(Graph) || !Op->HasTypedField<EJson::Boolean>(TEXT("bCallInEditor")))
		{ Entry->SetStringField(TEXT("Error"), TEXT("SetFunctionCallInEditor requires an owned FunctionName and bCallInEditor")); return; }
		UK2Node_FunctionEntry* FunctionEntry = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Terminal = Cast<UK2Node_EditablePinBase>(Node))
				if (Terminal->UserDefinedPins.Num() > 0)
				{ Entry->SetStringField(TEXT("Error"), TEXT("CallInEditor requires a function without inputs/outputs")); return; }
			if (auto* Candidate = Cast<UK2Node_FunctionEntry>(Node)) FunctionEntry = Candidate;
		}
		if (!FunctionEntry) { Entry->SetStringField(TEXT("Error"), TEXT("Function entry is missing")); return; }
		FunctionEntry->Modify();
		FunctionEntry->MetaData.bCallInEditor = FNexusArgs(Op).Bool(TEXT("bCallInEditor"));
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		Entry->SetBoolField(TEXT("bCallInEditor"), FunctionEntry->MetaData.bCallInEditor);
		return;
	}
	if (Action == TEXT("SetVariableMetadata") || Action == TEXT("SetVariableReplication"))
	{
		const FNexusArgs Args(Op);
		const FName Name(*Args.Str(TEXT("VariableName")));
		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(BP, Name);
		if (VariableIndex == INDEX_NONE)
		{ Entry->SetStringField(TEXT("Error"), TEXT("Variable must be owned by this Blueprint")); return; }
		FBPVariableDescription& Variable = BP->NewVariables[VariableIndex];
		if (Action == TEXT("SetVariableMetadata"))
		{
			if (!Op->HasTypedField<EJson::String>(TEXT("ToolTip")))
			{ Entry->SetStringField(TEXT("Error"), TEXT("SetVariableMetadata requires ToolTip")); return; }
			BP->Modify();
			Variable.SetMetaData(TEXT("ToolTip"), Args.Str(TEXT("ToolTip")));
			Entry->SetStringField(TEXT("ToolTip"), Args.Str(TEXT("ToolTip")));
		}
		else
		{
			const FString Mode = Args.Str(TEXT("Replication"));
			const bool bReplicated = Mode.Equals(TEXT("Replicated"), ESearchCase::IgnoreCase);
			const bool bNotify = Mode.Equals(TEXT("RepNotify"), ESearchCase::IgnoreCase);
			if (!bReplicated && !bNotify && !Mode.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{ Entry->SetStringField(TEXT("Error"), TEXT("Replication must be None, Replicated or RepNotify")); return; }
			if (!BP->ParentClass || (!BP->ParentClass->IsChildOf(AActor::StaticClass()) && !BP->ParentClass->IsChildOf(UActorComponent::StaticClass()))
				|| ((bReplicated || bNotify) && (Variable.VarType.ContainerType == EPinContainerType::Set || Variable.VarType.ContainerType == EPinContainerType::Map)))
			{ Entry->SetStringField(TEXT("Error"), TEXT("Replication requires Actor/ActorComponent members and does not support Set/Map variables")); return; }
			const FName NotifyName = bNotify ? FName(*Args.Str(TEXT("RepNotifyFunction"))) : NAME_None;
			UFunction* Notify = bNotify && BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass->FindFunctionByName(NotifyName) : nullptr;
			UEdGraph* NotifyGraph = bNotify ? FNexusBlueprintGraphUtils::FindBPGraph(BP, NotifyName.ToString()) : nullptr;
			if (bNotify && (!Notify || Notify->NumParms != 0 || !NotifyGraph || !BP->FunctionGraphs.Contains(NotifyGraph)
				|| Notify->HasAnyFunctionFlags(FUNC_Net | FUNC_Static | FUNC_BlueprintPure)))
			{ Entry->SetStringField(TEXT("Error"), TEXT("RepNotifyFunction must name an existing owned non-pure function without inputs/outputs or RPC flags")); return; }
			BP->Modify();
			Variable.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
			if (bReplicated || bNotify) Variable.PropertyFlags |= CPF_Net;
			if (bNotify) Variable.PropertyFlags |= CPF_RepNotify;
			Variable.RepNotifyFunc = NotifyName;
			if (!bReplicated && !bNotify) Variable.ReplicationCondition = COND_None;
			Entry->SetStringField(TEXT("Replication"), bNotify ? TEXT("RepNotify") : bReplicated ? TEXT("Replicated") : TEXT("None"));
			Entry->SetStringField(TEXT("RepNotifyFunction"), NotifyName.IsNone() ? TEXT("") : NotifyName.ToString());
			Entry->SetStringField(TEXT("Note"), TEXT("Owning actor/component replication must be enabled separately; existing notify graphs are preserved."));
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		Entry->SetStringField(TEXT("VariableName"), Name.ToString());
		return;
	}
	if (Action == TEXT("ChangeVariableType"))
	{
		const FNexusArgs Args(Op);
		FString Error;
		if (!FNexusBlueprintEditUtils::ChangeVariableType(BP, Args.Str(TEXT("VariableName")), Args.Str(TEXT("VariableType")), Args.Bool(TEXT("bAllowPotentialDataLoss")), false, Entry, Error)) Entry->SetStringField(TEXT("Error"), Error);
		return;
	}
	if (Action == TEXT("RenameLocalVariable"))
	{
		const FNexusArgs Args(Op);
		FString Error;
		if (!FNexusBlueprintEditUtils::ValidateLeaf(BP, Error)) { Entry->SetStringField(TEXT("Error"), Error); return; }
		UEdGraph* Graph = FNexusBlueprintGraphUtils::FindBPGraph(BP, Args.Str(TEXT("FunctionName")));
		UFunction* Scope = Graph && BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass->FindFunctionByName(Graph->GetFName()) : nullptr;
		const FName OldName(*Args.Str(TEXT("VariableName")));
		const FString NewName = Args.Str(TEXT("NewVariableName"));
		FBPVariableDescription* Variable = Graph ? FBlueprintEditorUtils::FindLocalVariable(BP, Graph, OldName) : nullptr;
		FKismetNameValidator Validator(BP, OldName, Scope);
		if (!Scope || !Variable || NewName.IsEmpty() || Validator.IsValid(NewName) != EValidatorResult::Ok || OldName == FName(*NewName)
			|| FBlueprintEditorUtils::FindLocalVariable(BP, Graph, FName(*NewName)))
		{ Entry->SetStringField(TEXT("Error"), TEXT("RenameLocalVariable requires a function-local variable and an unused valid NewVariableName")); return; }
		const FGuid Guid = Variable->VarGuid;
		FBlueprintEditorUtils::RenameLocalVariable(BP, Scope, OldName, FName(*NewName));
		Entry->SetStringField(TEXT("VariableName"), OldName.ToString());
		Entry->SetStringField(TEXT("NewVariableName"), NewName);
		Entry->SetStringField(TEXT("Guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	if (Action == TEXT("RenameVariable"))
	{
		const FNexusArgs Args(Op);
		const FName OldName(*Args.Str(TEXT("VariableName")));
		const FString NewName = Args.Str(TEXT("NewVariableName"));
		if (OldName.IsNone() || NewName.IsEmpty() || FBlueprintEditorUtils::FindNewVariableIndex(BP, OldName) == INDEX_NONE)
		{ Entry->SetStringField(TEXT("Error"), TEXT("RenameVariable requires an owned member VariableName and NewVariableName")); return; }
		FKismetNameValidator Validator(BP, OldName);
		if (Validator.IsValid(NewName) != EValidatorResult::Ok || OldName == FName(*NewName))
		{ Entry->SetStringField(TEXT("Error"), TEXT("NewVariableName is invalid, unchanged or already in use")); return; }
		if (!BP->NewVariables[FBlueprintEditorUtils::FindNewVariableIndex(BP, OldName)].RepNotifyFunc.IsNone())
		{ Entry->SetStringField(TEXT("Error"), TEXT("RenameVariable does not support RepNotify members requiring an editor dialog")); return; }
        FString LeafError;
        if (!FNexusBlueprintEditUtils::ValidateLeaf(BP, LeafError)) { Entry->SetStringField(TEXT("Error"), LeafError); return; }
		FBlueprintEditorUtils::RenameMemberVariable(BP, OldName, FName(*NewName));
		Entry->SetStringField(TEXT("VariableName"), OldName.ToString());
		Entry->SetStringField(TEXT("NewVariableName"), NewName);
		return;
	}
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
			// 默认值必须在引擎结构更新前写入，避免已存在子类先继承到零值。
			if (!FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType, FNexusArgs(Op).Str(TEXT("DefaultValue"))))
			{ Entry->SetStringField(TEXT("Error"), TEXT("Could not add variable; name may conflict with an inherited member")); return; }
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
					Var.PropertyFlags &= ~CPF_DisableEditOnInstance;
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
			FBlueprintEditorUtils::RemoveGraph(BP, Found, EGraphRemoveFlags::MarkTransient);
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
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
	return;
	}
#else
	(void)Op; (void)Ctx;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ManageAssetBlueprint only available in editor builds"));
#endif
}

#if WITH_EDITOR
struct FNexusScsAttachTarget
{
	USCS_Node* OwnedNode = nullptr;
	FName ParentVar = NAME_None;
	FName OwnerClassName = NAME_None;
	bool bNative = false;
	bool bResolved = false;
};

/** 本蓝图 SCS、父蓝图 SCS、C++ 原生组件，三者按这个顺序解析挂载目标。 */
static bool ResolveScsAttachTarget(UBlueprint* BP, const FString& AttachTo, FNexusScsAttachTarget& Out, FString& OutError)
{
	Out = FNexusScsAttachTarget();
	if (!BP || !BP->SimpleConstructionScript)
	{
		OutError = TEXT("Blueprint has no SimpleConstructionScript");
		return false;
	}
	if (USCS_Node* Node = BP->SimpleConstructionScript->FindSCSNode(*AttachTo))
	{
		Out.OwnedNode = Node;
		Out.bResolved = true;
		return true;
	}
	for (UClass* Cls = BP->ParentClass; Cls; Cls = Cls->GetSuperClass())
	{
		UBlueprint* ParentBP = Cast<UBlueprint>(Cls->ClassGeneratedBy);
		if (!ParentBP || !ParentBP->SimpleConstructionScript)
		{
			continue;
		}
		if (USCS_Node* Node = ParentBP->SimpleConstructionScript->FindSCSNode(*AttachTo))
		{
			Out.ParentVar = Node->GetVariableName();
			Out.OwnerClassName = Cls->GetFName();
			Out.bResolved = true;
			return true;
		}
	}
	UClass* NativeAncestor = BP->ParentClass;
	while (NativeAncestor && !NativeAncestor->HasAnyClassFlags(CLASS_Native))
	{
		NativeAncestor = NativeAncestor->GetSuperClass();
	}
	AActor* NativeActor = NativeAncestor ? Cast<AActor>(NativeAncestor->GetDefaultObject(false)) : nullptr;
	if (NativeActor)
	{
		TInlineComponentArray<UActorComponent*> Comps;
		NativeActor->GetComponents(Comps);
		for (UActorComponent* Comp : Comps)
		{
			if (Comp && Comp->GetName() == AttachTo)
			{
				Out.ParentVar = Comp->GetFName();
				Out.OwnerClassName = NativeAncestor->GetFName();
				Out.bNative = true;
				Out.bResolved = true;
				return true;
			}
		}
	}
	OutError = FString::Printf(TEXT("AttachTo component '%s' not found"), *AttachTo);
	return false;
}

static void DetachScsNode(USimpleConstructionScript* SCS, USCS_Node* Node)
{
	if (!SCS || !Node)
	{
		return;
	}
	// 引擎 API 同时移除根/子节点记录；稍后统一重新登记，保留整个子树。
	Node->Modify();
	SCS->RemoveNode(Node, false);
	Node->ParentComponentOrVariableName = NAME_None;
	Node->ParentComponentOwnerClassName = NAME_None;
	Node->bIsParentComponentNative = false;
}

static void ApplyScsAttach(USimpleConstructionScript* SCS, USCS_Node* Node, const FNexusScsAttachTarget& Target)
{
	if (Target.OwnedNode)
	{
		Target.OwnedNode->AddChildNode(Node);
		return;
	}
	Node->ParentComponentOrVariableName = Target.ParentVar;
	Node->ParentComponentOwnerClassName = Target.OwnerClassName;
	Node->bIsParentComponentNative = Target.bNative;
	SCS->AddNode(Node);
}

static USCS_Node* FindScsNodeInParents(UBlueprint* BP, const FString& ComponentName)
{
	if (!BP)
	{
		return nullptr;
	}
	if (BP->SimpleConstructionScript)
	{
		if (USCS_Node* Node = BP->SimpleConstructionScript->FindSCSNode(*ComponentName))
		{
			return Node;
		}
	}
	for (UClass* Cls = BP->ParentClass; Cls; Cls = Cls->GetSuperClass())
	{
		UBlueprint* ParentBP = Cast<UBlueprint>(Cls->ClassGeneratedBy);
		if (ParentBP && ParentBP->SimpleConstructionScript)
		{
			if (USCS_Node* Node = ParentBP->SimpleConstructionScript->FindSCSNode(*ComponentName))
			{
				return Node;
			}
		}
	}
	return nullptr;
}

/** 硬对象引用如果指到来源组件模板，按变量名重映射；对不上的清空并记录。软引用保持原路径。 */
static UActorComponent* MapCopiedComponentRef(
	UBlueprint* SourceBP, UBlueprint* DestBP, UObject* Value,
	const FString& SourceComponentName, USCS_Node* NewNode, FString& OutVarName)
{
	OutVarName.Reset();
	if (!SourceBP || !SourceBP->SimpleConstructionScript || !Value)
	{
		return nullptr;
	}
	for (USCS_Node* Node : SourceBP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate == Value)
		{
			OutVarName = Node->GetVariableName().ToString();
			break;
		}
	}
	if (OutVarName.IsEmpty())
	{
		return nullptr;
	}
	if (OutVarName == SourceComponentName && NewNode)
	{
		return NewNode->ComponentTemplate;
	}
	if (DestBP && DestBP->SimpleConstructionScript)
	{
		if (USCS_Node* DestNode = DestBP->SimpleConstructionScript->FindSCSNode(*OutVarName))
		{
			return DestNode->ComponentTemplate;
		}
	}
	return nullptr;
}

static void RemapCopiedComponentRefs(
	UBlueprint* SourceBP, UBlueprint* DestBP, UActorComponent* DestTemplate,
	const FString& SourceComponentName, USCS_Node* NewNode, TArray<FString>& Unmapped)
{
	if (!SourceBP || !SourceBP->SimpleConstructionScript || !DestTemplate)
	{
		return;
	}
	auto RemapOne = [&](FObjectProperty* ObjProp, void* Container)
	{
		UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(Container);
		FString VarName;
		if (UActorComponent* Mapped = MapCopiedComponentRef(SourceBP, DestBP, Value, SourceComponentName, NewNode, VarName))
		{
			ObjProp->SetObjectPropertyValue_InContainer(Container, Mapped);
		}
		else if (!VarName.IsEmpty())
		{
			ObjProp->SetObjectPropertyValue_InContainer(Container, nullptr);
			Unmapped.Add(ObjProp->GetName() + TEXT("->") + VarName);
		}
	};
	for (TFieldIterator<FProperty> It(DestTemplate->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			continue;
		}
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			RemapOne(ObjProp, DestTemplate);
			continue;
		}
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
		FObjectProperty* Inner = ArrayProp ? CastField<FObjectProperty>(ArrayProp->Inner) : nullptr;
		if (!ArrayProp || !Inner)
		{
			continue;
		}
		FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(DestTemplate));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			UObject* Value = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index));
			FString VarName;
			if (UActorComponent* Mapped = MapCopiedComponentRef(SourceBP, DestBP, Value, SourceComponentName, NewNode, VarName))
			{
				Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), Mapped);
			}
			else if (!VarName.IsEmpty())
			{
				Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), nullptr);
				Unmapped.Add(FString::Printf(TEXT("%s[%d]->%s"), *Prop->GetName(), Index, *VarName));
			}
		}
	}
}
#endif

static void HandleBP_SCS(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UBlueprint* BP = static_cast<UBlueprint*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	const FString& Action = Ctx.Action;
	if (Action == TEXT("ResetComponentPropertyToDefault"))
	{
		const FNexusArgs Args(Op);
		FString Error;
		UActorComponent* Component = FNexusBlueprintEditUtils::FindComponent(BP, Args.Str(TEXT("ComponentName")), false, Error);
		UActorComponent* Baseline = Component ? FNexusBlueprintEditUtils::FindComponentBaseline(BP, Component, Args.Str(TEXT("ComponentName")), Args.Str(TEXT("Baseline"), TEXT("ParentTemplate")), Error) : nullptr;
		TArray<FString> Segments;
		Args.Str(TEXT("PropertyPath")).ParseIntoArray(Segments, TEXT("."), false);
		auto ResolveOwned = [&](UActorComponent* Root, FProperty*& Property, void*& Value, UObject*& Owner)
		{
			if (!Root || !FNexusPropertyUtils::ResolvePropertyWrite(Root, Segments, 0, Property, Value, Error, &Owner)) return false;
			if (Owner != Root && !Owner->IsIn(Root))
			{ Error = TEXT("Component reset cannot traverse external object or asset references"); return false; }
			return true;
		};
		FProperty* SourceProperty = nullptr;
		void* Source = nullptr;
		UObject* SourceOwner = nullptr;
		if (!ResolveOwned(Baseline, SourceProperty, Source, SourceOwner)) { Entry->SetStringField(TEXT("Error"), Error); return; }
		if (SourceProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference)) { Entry->SetStringField(TEXT("Error"), TEXT("Instanced subobjects cannot be reset by copying a parent reference")); return; }
		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(SourceProperty))
			if (Cast<UActorComponent>(ObjectProperty->GetObjectPropertyValue(Source))) { Entry->SetStringField(TEXT("Error"), TEXT("Component references must be remapped explicitly")); return; }
		FProperty* Property = nullptr;
		void* Target = nullptr;
		UObject* TargetOwner = nullptr;
		// 先验证当前目标，避免错误路径先创建继承覆盖模板。
		if (!ResolveOwned(Component, Property, Target, TargetOwner) || Property != SourceProperty) { Entry->SetStringField(TEXT("Error"), Error.IsEmpty() ? TEXT("Baseline property type differs") : Error); return; }
		Component = FNexusBlueprintEditUtils::FindComponent(BP, Args.Str(TEXT("ComponentName")), true, Error);
		if (!ResolveOwned(Component, Property, Target, TargetOwner) || Property != SourceProperty) { Entry->SetStringField(TEXT("Error"), Error.IsEmpty() ? TEXT("Baseline property type differs") : Error); return; }
		FString Previous, Actual;
		FNexusPropertyUtils::ExportText(Property, Previous, Target);
		TargetOwner->SetFlags(RF_Transactional);
		TargetOwner->Modify();
		Property->CopyCompleteValue(Target, Source);
		FNexusPropertyUtils::ExportText(Property, Actual, Target);
		Entry->SetStringField(TEXT("OldValue"), Previous);
		Entry->SetStringField(TEXT("NewValue"), Actual);
		Entry->SetStringField(TEXT("BaselinePath"), Baseline->GetPathName());
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		return;
	}

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
	return;
	}

	if (Action == TEXT("AddComponent") || Action == TEXT("RemoveComponent") ||
	    Action == TEXT("SetComponentProperty") || Action == TEXT("CopyComponent") ||
	    Action == TEXT("ReattachComponent"))
	{
		if (!BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()))
		{
			const FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("(none)");
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("Blueprint parent is not Actor subclass: %s (Parent=%s). Component actions need an Actor BP; use AddVariable/AddFunction/AddNode for GA/UI/BPI, or ManageAssetGameplayAbility for GAS semantic fields."),
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
				FNexusScsAttachTarget Target;
				FString AttachErr;
				if (!ResolveScsAttachTarget(BP, AttachTo, Target, AttachErr))
				{
					if (NewNode->ComponentTemplate)
					{
						NewNode->ComponentTemplate->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
					}
					NewNode->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
					Entry->SetStringField(TEXT("Error"), AttachErr);
					return;
				}
				ApplyScsAttach(SCS, NewNode, Target);
			}
			else
			{
				USCS_Node* DefaultRoot = SCS->GetDefaultSceneRootNode();
				if (DefaultRoot) DefaultRoot->AddChildNode(NewNode);
				else             SCS->AddNode(NewNode);
			}

			Entry->SetStringField(TEXT("ComponentName"),  ComponentName);
			Entry->SetStringField(TEXT("ComponentClass"), CompClass->GetName());
			// 这里只刷新 skeleton，完整编译由 bCompile 在批次收尾统一执行。
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
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
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	return;
		}

        if (Action == TEXT("SetComponentProperty"))
        {
            const FNexusArgs Args(Op);
            if (FNexusAssetEditUtils::IsVerified(Ctx.Args) || Op->HasField(TEXT("ExpectedValue")))
            {
                FNexusAssetEditUtils::Apply(BP, TEXT("ManageAssetBlueprint"), Op, FNexusAssetEditUtils::IsVerified(Ctx.Args), Entry);
                return;
            }
            FString Error;
            UActorComponent* Component = FNexusBlueprintEditUtils::FindComponent(BP, Args.Str(TEXT("ComponentName")), true, Error);
            if (!Component || !Op->HasTypedField<EJson::String>(TEXT("Value"))) { Entry->SetStringField(TEXT("Error"), Error.IsEmpty() ? TEXT("Value is required") : Error); return; }
            Component->SetFlags(RF_Transactional);
            Component->Modify();
            FString Previous, Actual;
            if (!FNexusPropertyUtils::WritePropertyAndEcho(Component, {Args.Str(TEXT("PropertyPath"))}, 0, Args.Str(TEXT("Value")), Previous, Actual, Error)) { Entry->SetStringField(TEXT("Error"), Error); return; }
            Entry->SetStringField(TEXT("OldValue"), Previous);
            Entry->SetStringField(TEXT("NewValue"), Actual);
            Entry->SetStringField(TEXT("ComponentName"), Args.Str(TEXT("ComponentName")));
            Entry->SetStringField(TEXT("PropertyPath"), Args.Str(TEXT("PropertyPath")));
            Component->MarkPackageDirty();
            FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
            return;
        }

		if (Action == TEXT("CopyComponent") || Action == TEXT("ReattachComponent"))
		{
			USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
			if (!SCS)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Blueprint has no SimpleConstructionScript"));
				return;
			}
			FString ComponentName;
			Op->TryGetStringField(TEXT("ComponentName"), ComponentName);

			if (Action == TEXT("ReattachComponent"))
			{
				if (ComponentName.IsEmpty())
				{
					Entry->SetStringField(TEXT("Error"), TEXT("ReattachComponent requires ComponentName"));
					return;
				}
				FString AttachTo;
				Op->TryGetStringField(TEXT("AttachTo"), AttachTo);
				if (AttachTo.IsEmpty())
				{
					Entry->SetStringField(TEXT("Error"), TEXT("ReattachComponent requires AttachTo"));
					return;
				}
				USCS_Node* Node = SCS->FindSCSNode(*ComponentName);
				if (!Node)
				{
					Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Component '%s' not found"), *ComponentName));
					return;
				}
				FNexusScsAttachTarget Target;
				FString AttachErr;
				if (!ResolveScsAttachTarget(BP, AttachTo, Target, AttachErr))
				{
					Entry->SetStringField(TEXT("Error"), AttachErr);
					return;
				}
				if (Target.OwnedNode == Node)
				{
					Entry->SetStringField(TEXT("Error"), TEXT("AttachTo cannot be the component itself"));
					return;
				}
				if (Target.OwnedNode && Target.OwnedNode->IsChildOf(Node))
				{
					Entry->SetStringField(TEXT("Error"), TEXT("AttachTo cannot be a descendant of the component"));
					return;
				}
				DetachScsNode(SCS, Node);
				ApplyScsAttach(SCS, Node, Target);
				Entry->SetStringField(TEXT("ComponentName"), ComponentName);
				Entry->SetStringField(TEXT("AttachTo"), AttachTo);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
				return;
			}

			FString SourceAssetPath, SourceComponentName;
			Op->TryGetStringField(TEXT("SourceAssetPath"), SourceAssetPath);
			Op->TryGetStringField(TEXT("SourceComponentName"), SourceComponentName);
			if (SourceAssetPath.IsEmpty() || SourceComponentName.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("CopyComponent requires SourceAssetPath and SourceComponentName"));
				return;
			}
			if (ComponentName.IsEmpty())
			{
				ComponentName = SourceComponentName;
			}
			if (SCS->FindSCSNode(*ComponentName))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Component '%s' already exists"), *ComponentName));
				return;
			}
			UBlueprint* SourceBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(SourceAssetPath);
			if (!SourceBP)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Source Blueprint not found: %s"), *SourceAssetPath));
				return;
			}
			USCS_Node* SourceNode = FindScsNodeInParents(SourceBP, SourceComponentName);
			if (!SourceNode || !SourceNode->ComponentTemplate)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Source component '%s' not found"), *SourceComponentName));
				return;
			}
			UBlueprintGeneratedClass* SourceClass = Cast<UBlueprintGeneratedClass>(SourceBP->GeneratedClass);
			UActorComponent* SourceTemplate = SourceClass ? SourceNode->GetActualComponentTemplate(SourceClass) : nullptr;
			if (!SourceTemplate)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Source component has no effective template"));
				return;
			}
			USCS_Node* NewNode = SCS->CreateNode(SourceTemplate->GetClass(), *ComponentName);
			if (!NewNode || !NewNode->ComponentTemplate)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Create SCS node failed"));
				return;
			}
			if (GEditor)
			{
				GEditor->CopyPropertiesForUnrelatedObjects(SourceTemplate, NewNode->ComponentTemplate);
			}
			FString AttachTo;
			Op->TryGetStringField(TEXT("AttachTo"), AttachTo);
			if (!AttachTo.IsEmpty())
			{
				FNexusScsAttachTarget Target;
				FString AttachErr;
				if (!ResolveScsAttachTarget(BP, AttachTo, Target, AttachErr))
				{
					if (NewNode->ComponentTemplate)
					{
						NewNode->ComponentTemplate->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
					}
					NewNode->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
					Entry->SetStringField(TEXT("Error"), AttachErr);
					return;
				}
				ApplyScsAttach(SCS, NewNode, Target);
			}
			else
			{
				USCS_Node* DefaultRoot = SCS->GetDefaultSceneRootNode();
				if (DefaultRoot) DefaultRoot->AddChildNode(NewNode);
				else SCS->AddNode(NewNode);
			}
			TArray<FString> Unmapped;
			RemapCopiedComponentRefs(SourceBP, BP, NewNode->ComponentTemplate, SourceComponentName, NewNode, Unmapped);
			Entry->SetStringField(TEXT("ComponentName"), ComponentName);
			Entry->SetStringField(TEXT("ComponentClass"), NewNode->ComponentTemplate->GetClass()->GetName());
			Entry->SetStringField(TEXT("SourceComponentName"), SourceComponentName);
			if (Unmapped.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> UnmappedJson;
				for (const FString& Item : Unmapped)
				{
					UnmappedJson.Add(MakeShared<FJsonValueString>(Item));
				}
				Entry->SetArrayField(TEXT("UnmappedRefs"), UnmappedJson);
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
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
	if (!FNexusAssetStructureUtils::Check(BP, TEXT("ManageAssetBlueprint"), Args, OutError)) return false;
	return true;
#else
	OutError = TEXT("ManageAssetBlueprint only available in editor builds");
	return false;
#endif
}

void FNexusManageAssetBlueprintCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
    for (const FString& Action : {TEXT("SetVariableMetadata"),TEXT("SetVariableReplication"),TEXT("SetFunctionCallInEditor")}) OutHandlers.Add(Action, &HandleBP_Variable);
    for (const FString& Action : {TEXT("ArrangeNodes"),TEXT("WriteGraphDSL"),TEXT("WriteGraphPlan"),TEXT("AddFunctionParameter"),TEXT("RemoveFunctionParameter"),TEXT("SetVariableCategory"),TEXT("SetVariableEditable")}) OutHandlers.Add(Action, &FNexusBlueprintGraphEnhancements::Write);
	OutHandlers.Add(TEXT("AddVariable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("RemoveVariable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("RenameVariable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("RenameLocalVariable"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("ChangeVariableType"), &HandleBP_Variable);
	OutHandlers.Add(TEXT("Reparent"), &HandleBP_Reparent);
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
	OutHandlers.Add(TEXT("ResetComponentPropertyToDefault"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("CopyComponent"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("ReattachComponent"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("SetDefaults"), &HandleBP_SCS);
	OutHandlers.Add(TEXT("AddNode"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("RemoveNode"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("SetNode"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("PromotePin"), &FNexusBlueprintGraphOps::HandleGraphNode);
	OutHandlers.Add(TEXT("Connect"), &FNexusBlueprintGraphOps::HandleWire);
	OutHandlers.Add(TEXT("Disconnect"), &FNexusBlueprintGraphOps::HandleWire);
	OutHandlers.Add(TEXT("DisconnectAll"), &FNexusBlueprintGraphOps::HandleWire);
	FNexusAssetStructureUtils::Wrap(TEXT("ManageAssetBlueprint"), OutHandlers, [](FNexusActionContext& Context) -> UObject* { return static_cast<UBlueprint*>(Context.Target); });
}

void FNexusManageAssetBlueprintCapability::Finalize(const TSharedPtr<FJsonObject>& Arguments, FCapabilityResult& Result) const
{
	if (!FNexusAssetEditUtils::IsVerified(Arguments))
	{ FNexusCapability::Finalize(Arguments, Result); return; }
	if (Result.HasErrors()) return;
	// bCompile 完全由调用者决定。编译最多一次，之后重新解析属性，验证成功才保存。
	TSharedPtr<FJsonObject> CompileArgs = MakeShared<FJsonObject>(*Arguments);
	CompileArgs->SetBoolField(TEXT("bVerify"), false);
	CompileArgs->SetBoolField(TEXT("bSaveToDisk"), false);
	FNexusCapability::Finalize(CompileArgs, Result);
	if (Result.HasErrors()) return;
	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(FNexusArgs(Arguments).Str(TEXT("AssetPath")));
	if (!BP) { Result.FatalError = TEXT("Blueprint unavailable during verification"); return; }
	TSet<FString> VerifiedPaths;
	for (int32 EntryIndex = Result.Entries.Num() - 1; EntryIndex >= 0; --EntryIndex)
	{
		TSharedPtr<FJsonObject> Entry = Result.Entries[EntryIndex]->AsObject();
		if (!Entry->HasField(TEXT("bVerified"))) continue;
		const FString PropertyPath = FNexusArgs(Entry).Str(TEXT("ComponentName")) + TEXT(":") + FNexusArgs(Entry).Str(TEXT("PropertyPath"));
		// 同一属性可顺序写多次：前项已做即时回读，收尾只验证最后一次写入。
		if (VerifiedPaths.Contains(PropertyPath)) continue;
		VerifiedPaths.Add(PropertyPath);
		FNexusAssetEditUtils::Verify(BP, TEXT("ManageAssetBlueprint"), Entry, Entry);
	}
	if (Result.HasErrors()) return;
	TSharedPtr<FJsonObject> SaveArgs = MakeShared<FJsonObject>(*Arguments);
	SaveArgs->SetBoolField(TEXT("bVerify"), false);
	SaveArgs->SetBoolField(TEXT("bCompile"), false);
	FNexusCapability::Finalize(SaveArgs, Result);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetBlueprintCapability)
