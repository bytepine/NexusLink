// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusManageAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "StateTree.h"
#if WITH_EDITOR
#include "Capabilities/Asset/StateTree/NexusStateTreeTraversal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditorNode.h"
#include "StateTreeTaskBase.h"
#endif

// ── StateTree 编辑仅在编辑器可用 ─────────────────────────────────────────────

void FNexusManageAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetStateTree");
	Out.SearchAssetTypes = {TEXT("StateTree")};
	Out.Description = TEXT("Edit StateTree: state/task/condition/transition. UE 5.5+.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("AddState"), TEXT("RemoveState"), TEXT("RenameState"), TEXT("Recompile"),
			  TEXT("AddTask"), TEXT("RemoveTask"),
			  TEXT("AddEnterCondition"), TEXT("RemoveEnterCondition"),
			  TEXT("AddTransition"), TEXT("RemoveTransition") }))
		.Prop(TEXT("StateName"),    FNexusSchema::Str(TEXT("Target State name")))
		.Prop(TEXT("NewName"),      FNexusSchema::Str(TEXT("RenameState: new name")))
		.Prop(TEXT("ParentState"),  FNexusSchema::Str(TEXT("AddState: parent State name (empty=top SubTree)")))
		.Prop(TEXT("StateType"),    FNexusSchema::Enum(
			TEXT("AddState: state type"),
			{ TEXT("State"), TEXT("Group"), TEXT("Linked"), TEXT("Subtree") },
			TEXT("State")))
		.Prop(TEXT("NodeType"),     FNexusSchema::Str(TEXT("Task/Condition UScriptStruct short name")))
		.Prop(TEXT("TargetState"),  FNexusSchema::Str(TEXT("AddTransition target State")))
		.Prop(TEXT("Index"),        FNexusSchema::Int(TEXT("RemoveTask/condition/transition index")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("StateTree asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("State"), TEXT("st"), TEXT("task"), TEXT("Transition"), TEXT("npc"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("GetAssetStateTree"), TEXT("CreateAssetStateTree"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Add/remove/edit StateTree State/Task/Condition/Transition");
}

#if WITH_EDITOR
static UStateTree* STFrom(FNexusActionContext& Ctx)
{
	return static_cast<UStateTree*>(Ctx.Target);
}

static UStateTreeEditorData* EdFrom(FNexusActionContext& Ctx)
{
	UStateTree* ST = STFrom(Ctx);
	return ST ? Cast<UStateTreeEditorData>(ST->EditorData) : nullptr;
}

static UStateTreeState* FindStateByName(FNexusActionContext& Ctx, const FString& Name)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	if (!EdData || Name.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("A state name and StateTree editor data are required"));
		return nullptr;
	}
	const FName RequestedName(*Name);
	UStateTreeState* Match = nullptr;
	bool bAmbiguous = false;
	VisitNexusStateTreeStates(*EdData, [&](UStateTreeState& State)
	{
		if (State.Name == RequestedName)
		{
			if (Match)
			{
				bAmbiguous = true;
				return EStateTreeVisitor::Break;
			}
			Match = &State;
		}
		return EStateTreeVisitor::Continue;
	});
	if (bAmbiguous)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Ambiguous state name: %s"), *Name));
		return nullptr;
	}
	if (!Match) Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State not found: %s"), *Name));
	return Match;
}

static void MarkSTDirty(FNexusActionContext& Ctx)
{
	if (UStateTree* ST = STFrom(Ctx))
	{
		ST->MarkPackageDirty();
	}
}

static void ModifyStateTreeObject(UObject& Owner)
{
	Owner.SetFlags(RF_Transactional);
	Owner.Modify();
}

static UScriptStruct* FindStateTreeNodeStruct(const FString& NodeType)
{
	FString StructName = NodeType;
	if (!StructName.StartsWith(TEXT("F"))) StructName = TEXT("F") + StructName;
#if NX_UE_HAS_FIND_FIRST_OBJECT
	UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::NativeFirst);
	if (!Struct) Struct = FindFirstObject<UScriptStruct>(*NodeType, EFindFirstObjectOptions::NativeFirst);
#else
	UScriptStruct* Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *StructName);
	if (!Struct) Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *NodeType);
#endif
	return Struct;
}

static void HandleST_AddState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	if (!EdData)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("StateTreeEditorData is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddState requires StateName"));
		return;
	}
	EStateTreeStateType StateType = EStateTreeStateType::State;
	const FString TypeStr = A.Str(TEXT("StateType"));
	if (TypeStr == TEXT("Group"))       StateType = EStateTreeStateType::Group;
	else if (TypeStr == TEXT("Linked"))  StateType = EStateTreeStateType::Linked;
	else if (TypeStr == TEXT("Subtree")) StateType = EStateTreeStateType::Subtree;

	const FString ParentName = A.Str(TEXT("ParentState"));
	UStateTreeState* ParentState = ParentName.IsEmpty() ? nullptr : FindStateByName(Ctx, ParentName);
	if (!ParentName.IsEmpty() && !ParentState) return;
	if (ParentState)
	{
		ModifyStateTreeObject(*ParentState);
		ParentState->AddChildState(*StateName, StateType);
	}
	else
	{
		ModifyStateTreeObject(*EdData);
		EdData->AddSubTree(*StateName).Type = StateType;
	}
	Ctx.Entry->SetStringField(TEXT("AddedState"), StateName);
	MarkSTDirty(Ctx);
}

static void HandleST_RemoveState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	if (!EdData)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("StateTreeEditorData is null"));
		return;
	}
	const FString StateName = FNexusArgs(Op).Str(TEXT("StateName"));
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveState requires StateName"));
		return;
	}
	UStateTreeState* Target = FindStateByName(Ctx, StateName);
	if (!Target) return;
	auto RemoveFrom = [Target](UObject& Owner, TArray<TObjectPtr<UStateTreeState>>& States)
	{
		if (!States.Contains(Target)) return false;
		ModifyStateTreeObject(Owner);
		States.Remove(Target);
		return true;
	};
	bool bRemoved = RemoveFrom(*EdData, EdData->SubTrees);
	if (!bRemoved)
	{
		// 旧资产的 Parent 可能为空，以实际持有 Children 的状态为准。
		VisitNexusStateTreeStates(*EdData, [&](UStateTreeState& State)
		{
			bRemoved = RemoveFrom(State, State.Children);
			return bRemoved ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
		});
	}
	if (bRemoved) MarkSTDirty(Ctx);
	else Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveState failed"));
}

static void HandleST_RenameState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const FString NewName = A.Str(TEXT("NewName"));
	if (StateName.IsEmpty() || NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RenameState requires StateName and NewName"));
		return;
	}
	UStateTreeState* Target = FindStateByName(Ctx, StateName);
	if (!Target) return;
	ModifyStateTreeObject(*Target);
	Target->Name = *NewName;
	MarkSTDirty(Ctx);
}

static void HandleST_Recompile(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	MarkSTDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("Marked dirty; reopen editor or SaveAsset to recompile"));
}

static void AddStateTreeEditorNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, bool bTask)
{
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const FString NodeType = A.Str(TEXT("NodeType"));
	UStateTreeState* Target = FindStateByName(Ctx, StateName);
	if (!Target) return;
	if (NodeType.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("StateName and NodeType required"));
		return;
	}
	UScriptStruct* Struct = FindStateTreeNodeStruct(NodeType);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("struct not found: %s"), *NodeType));
		return;
	}
	if (!Struct->IsChildOf(bTask ? FStateTreeTaskBase::StaticStruct() : FStateTreeConditionBase::StaticStruct()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("%s is not a StateTree %s"), *NodeType, bTask ? TEXT("task") : TEXT("condition")));
		return;
	}
	FStateTreeEditorNode NewNode;
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(Struct);
	auto InitializeData = [Target](const UStruct* Type, FInstancedStruct& Instance, TObjectPtr<UObject>& InstanceObject)
	{
		if (const UScriptStruct* InstanceType = Cast<UScriptStruct>(Type))
		{
			Instance.InitializeAs(InstanceType);
			return true;
		}
		const UClass* InstanceClass = Cast<UClass>(Type);
		if (!InstanceClass || InstanceClass->HasAnyClassFlags(CLASS_Abstract)) return false;
		InstanceObject = NewObject<UObject>(Target, InstanceClass, NAME_None, RF_Transactional);
		return true;
	};
	const FStateTreeNodeBase& Node = NewNode.Node.Get<FStateTreeNodeBase>();
	if (!InitializeData(Node.GetInstanceDataType(), NewNode.Instance, NewNode.InstanceObject))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("%s requires a concrete instance data type"), *NodeType));
		return;
	}
#if NX_UE_HAS_STATETREE_EXECUTION_RUNTIME_DATA
	const UStruct* RuntimeType = Node.GetExecutionRuntimeDataType();
	if (RuntimeType && !InitializeData(RuntimeType, NewNode.ExecutionRuntimeData, NewNode.ExecutionRuntimeDataObject))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("%s requires a concrete execution runtime data type"), *NodeType));
		return;
	}
#endif
	ModifyStateTreeObject(*Target);
	TArray<FStateTreeEditorNode>& Nodes = bTask ? Target->Tasks : Target->EnterConditions;
	Nodes.Add(MoveTemp(NewNode));
	Ctx.Entry->SetNumberField(bTask ? TEXT("TasksCount") : TEXT("EnterConditionsCount"), Nodes.Num());
	Ctx.Entry->SetStringField(TEXT("NodeType"), Struct->GetName());
	MarkSTDirty(Ctx);
}

static void HandleST_AddTask(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	AddStateTreeEditorNode(Op, Ctx, /*bTask=*/true);
}

static void HandleST_AddEnterCondition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	AddStateTreeEditorNode(Op, Ctx, /*bTask=*/false);
}

static void RemoveStateTreeIndexed(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, bool bTask)
{
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const int32 Idx = Op->HasField(TEXT("Index")) ? static_cast<int32>(A.Num(TEXT("Index"))) : 0;
	UStateTreeState* Target = FindStateByName(Ctx, StateName);
	if (!Target) return;
	TArray<FStateTreeEditorNode>& Arr = bTask ? Target->Tasks : Target->EnterConditions;
	if (!Arr.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("index out of bounds"));
		return;
	}
	ModifyStateTreeObject(*Target);
	Arr.RemoveAt(Idx);
	MarkSTDirty(Ctx);
}

static void HandleST_RemoveTask(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	RemoveStateTreeIndexed(Op, Ctx, /*bTask=*/true);
}

static void HandleST_RemoveEnterCondition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	RemoveStateTreeIndexed(Op, Ctx, /*bTask=*/false);
}

static void HandleST_AddTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const FString TargetName = A.Str(TEXT("TargetState"));
	UStateTreeState* Source = FindStateByName(Ctx, StateName);
	if (!Source) return;
	UStateTreeState* Dest = FindStateByName(Ctx, TargetName);
	if (!Dest) return;
	ModifyStateTreeObject(*Source);
	Source->AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, Dest);
	Ctx.Entry->SetStringField(TEXT("TargetState"), TargetName);
	MarkSTDirty(Ctx);
}

static void HandleST_RemoveTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const int32 Idx = Op->HasField(TEXT("Index")) ? static_cast<int32>(A.Num(TEXT("Index"))) : 0;
	UStateTreeState* Source = FindStateByName(Ctx, StateName);
	if (!Source) return;
	if (!Source->Transitions.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTransition requires valid StateName and index"));
		return;
	}
	ModifyStateTreeObject(*Source);
	Source->Transitions.RemoveAt(Idx);
	MarkSTDirty(Ctx);
}
#endif // WITH_EDITOR

bool FNexusManageAssetStateTreeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UStateTree* ST = FNexusAssetUtils::LoadAssetWithFallback<UStateTree>(AssetPath);
	if (!ST)
	{
		OutError = FString::Printf(TEXT("StateTree not found: %s"), *AssetPath);
		return false;
	}
	if (!Cast<UStateTreeEditorData>(ST->EditorData))
	{
		OutError = TEXT("StateTree has no editor data; editor builds only");
		return false;
	}
	OutTarget = ST;
	return true;
#else
	OutError = TEXT("ManageAssetStateTree only available in editor builds");
	return false;
#endif
}

void FNexusManageAssetStateTreeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("AddState"),              &HandleST_AddState);
	OutHandlers.Add(TEXT("RemoveState"),           &HandleST_RemoveState);
	OutHandlers.Add(TEXT("RenameState"),           &HandleST_RenameState);
	OutHandlers.Add(TEXT("Recompile"),              &HandleST_Recompile);
	OutHandlers.Add(TEXT("AddTask"),               &HandleST_AddTask);
	OutHandlers.Add(TEXT("RemoveTask"),            &HandleST_RemoveTask);
	OutHandlers.Add(TEXT("AddEnterCondition"),    &HandleST_AddEnterCondition);
	OutHandlers.Add(TEXT("RemoveEnterCondition"), &HandleST_RemoveEnterCondition);
	OutHandlers.Add(TEXT("AddTransition"),         &HandleST_AddTransition);
	OutHandlers.Add(TEXT("RemoveTransition"),      &HandleST_RemoveTransition);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetStateTreeCapability)

#endif // WITH_STATETREE
