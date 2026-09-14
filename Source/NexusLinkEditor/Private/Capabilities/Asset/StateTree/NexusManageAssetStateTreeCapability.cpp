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
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#endif

// ── StateTree 编辑仅在编辑器可用 ─────────────────────────────────────────────

void FManageAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_state_tree");
	Out.SearchAssetTypes = {TEXT("StateTree")};
	Out.Description = TEXT("Edit StateTree: state/task/condition/transition. UE 5.5+.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("add_state"), TEXT("remove_state"), TEXT("rename_state"), TEXT("recompile"),
			  TEXT("add_task"), TEXT("remove_task"),
			  TEXT("add_enter_condition"), TEXT("remove_enter_condition"),
			  TEXT("add_transition"), TEXT("remove_transition") }))
		.Prop(TEXT("stateName"),    FNexusSchema::Str(TEXT("Target State name")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("rename_state: new name")))
		.Prop(TEXT("parentState"),  FNexusSchema::Str(TEXT("add_state: parent State name (empty=top SubTree)")))
		.Prop(TEXT("stateType"),    FNexusSchema::Enum(
			TEXT("add_state: state type"),
			{ TEXT("State"), TEXT("Group"), TEXT("Linked"), TEXT("Subtree") },
			TEXT("State")))
		.Prop(TEXT("nodeType"),     FNexusSchema::Str(TEXT("Task/Condition UScriptStruct short name")))
		.Prop(TEXT("targetState"),  FNexusSchema::Str(TEXT("add_transition target State")))
		.Prop(TEXT("index"),        FNexusSchema::Int(TEXT("remove_task/condition/transition index")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("StateTree asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("state"), TEXT("st"), TEXT("task"), TEXT("transition"), TEXT("npc"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("get_asset_state_tree"), TEXT("create_asset_state_tree"), TEXT("save_asset") };
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

static UStateTreeState* FindStateByName(UStateTreeEditorData* EdData, const FString& Name)
{
	if (!EdData || Name.IsEmpty()) return nullptr;
	TArray<UStateTreeState*> Stack;
	for (UStateTreeState* Root : EdData->SubTrees)
	{
		if (Root) Stack.Add(Root);
	}
	while (Stack.Num() > 0)
	{
		UStateTreeState* S = Stack.Pop();
		if (S->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return S;
		}
		for (UStateTreeState* Child : S->Children)
		{
			if (Child) Stack.Add(Child);
		}
	}
	return nullptr;
}

static void MarkSTDirty(FNexusActionContext& Ctx)
{
	if (UStateTree* ST = STFrom(Ctx))
	{
		ST->MarkPackageDirty();
	}
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
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("stateName"));
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_state requires stateName"));
		return;
	}
	EStateTreeStateType StateType = EStateTreeStateType::State;
	const FString TypeStr = A.Str(TEXT("stateType"));
	if (TypeStr == TEXT("Group"))       StateType = EStateTreeStateType::Group;
	else if (TypeStr == TEXT("Linked"))  StateType = EStateTreeStateType::Linked;
	else if (TypeStr == TEXT("Subtree")) StateType = EStateTreeStateType::Subtree;

	UStateTreeState* NewState = NewObject<UStateTreeState>(EdData, NAME_None, RF_Transactional);
	NewState->Name = *StateName;
	NewState->Type = StateType;

	const FString ParentName = A.Str(TEXT("parentState"));
	if (!ParentName.IsEmpty())
	{
		UStateTreeState* ParentState = FindStateByName(EdData, ParentName);
		if (!ParentState)
		{
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent State not found: %s"), *ParentName));
			return;
		}
		NewState->Rename(nullptr, ParentState);
		ParentState->Children.Add(NewState);
	}
	else
	{
		NewState->Rename(nullptr, EdData);
		EdData->SubTrees.Add(NewState);
	}
	Ctx.Entry->SetStringField(TEXT("addedState"), StateName);
	MarkSTDirty(Ctx);
}

static void HandleST_RemoveState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FString StateName = FNexusArgs(Op).Str(TEXT("stateName"));
	if (StateName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_state requires stateName"));
		return;
	}
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State not found: %s"), *StateName));
		return;
	}
	bool bRemoved = false;
	if (EdData->SubTrees.Remove(Target) > 0)
	{
		bRemoved = true;
	}
	else
	{
		TArray<UStateTreeState*> Stack;
		for (UStateTreeState* Root : EdData->SubTrees)
		{
			if (Root) Stack.Add(Root);
		}
		while (Stack.Num() > 0 && !bRemoved)
		{
			UStateTreeState* S = Stack.Pop();
			if (S->Children.Remove(Target) > 0)
			{
				bRemoved = true;
				break;
			}
			for (UStateTreeState* Child : S->Children)
			{
				if (Child) Stack.Add(Child);
			}
		}
	}
	if (bRemoved) MarkSTDirty(Ctx);
	else Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_state failed"));
}

static void HandleST_RenameState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("stateName"));
	const FString NewName = A.Str(TEXT("newName"));
	if (StateName.IsEmpty() || NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("rename_state requires stateName and newName"));
		return;
	}
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("State not found: %s"), *StateName));
		return;
	}
	Target->Name = *NewName;
	MarkSTDirty(Ctx);
}

static void HandleST_Recompile(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	MarkSTDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("Marked dirty; reopen editor or save_asset to recompile"));
}

static void AddStateTreeEditorNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, bool bTask)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("stateName"));
	const FString NodeType = A.Str(TEXT("nodeType"));
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target || NodeType.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("stateName and nodeType required"));
		return;
	}
	UScriptStruct* Struct = FindStateTreeNodeStruct(NodeType);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("struct not found: %s"), *NodeType));
		return;
	}
	FStateTreeEditorNode NewNode;
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(Struct);
	if (bTask)
	{
		Target->Tasks.Add(NewNode);
		Ctx.Entry->SetNumberField(TEXT("tasksCount"), Target->Tasks.Num());
	}
	else
	{
		Target->EnterConditions.Add(NewNode);
		Ctx.Entry->SetNumberField(TEXT("enterConditionsCount"), Target->EnterConditions.Num());
	}
	Ctx.Entry->SetStringField(TEXT("nodeType"), Struct->GetName());
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
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("stateName"));
	const int32 Idx = Op->HasField(TEXT("index")) ? static_cast<int32>(A.Num(TEXT("index"))) : 0;
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Valid stateName required"));
		return;
	}
	TArray<FStateTreeEditorNode>& Arr = bTask ? Target->Tasks : Target->EnterConditions;
	if (!Arr.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("index out of bounds"));
		return;
	}
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
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("stateName"));
	const FString TargetName = A.Str(TEXT("targetState"));
	UStateTreeState* Source = FindStateByName(EdData, StateName);
	UStateTreeState* Dest = FindStateByName(EdData, TargetName);
	if (!Source || !Dest)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_transition requires valid stateName and targetState"));
		return;
	}
	FStateTreeTransition Trans;
	Trans.Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	Trans.State.ID = Dest->ID;
	Source->Transitions.Add(Trans);
	Ctx.Entry->SetStringField(TEXT("targetState"), TargetName);
	MarkSTDirty(Ctx);
}

static void HandleST_RemoveTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("stateName"));
	const int32 Idx = Op->HasField(TEXT("index")) ? static_cast<int32>(A.Num(TEXT("index"))) : 0;
	UStateTreeState* Source = FindStateByName(EdData, StateName);
	if (!Source || !Source->Transitions.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_transition requires valid stateName and index"));
		return;
	}
	Source->Transitions.RemoveAt(Idx);
	MarkSTDirty(Ctx);
}
#endif // WITH_EDITOR

bool FManageAssetStateTreeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
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
	OutError = TEXT("manage_asset_state_tree only available in editor builds");
	return false;
#endif
}

void FManageAssetStateTreeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("add_state"),              &HandleST_AddState);
	OutHandlers.Add(TEXT("remove_state"),           &HandleST_RemoveState);
	OutHandlers.Add(TEXT("rename_state"),           &HandleST_RenameState);
	OutHandlers.Add(TEXT("recompile"),              &HandleST_Recompile);
	OutHandlers.Add(TEXT("add_task"),               &HandleST_AddTask);
	OutHandlers.Add(TEXT("remove_task"),            &HandleST_RemoveTask);
	OutHandlers.Add(TEXT("add_enter_condition"),    &HandleST_AddEnterCondition);
	OutHandlers.Add(TEXT("remove_enter_condition"), &HandleST_RemoveEnterCondition);
	OutHandlers.Add(TEXT("add_transition"),         &HandleST_AddTransition);
	OutHandlers.Add(TEXT("remove_transition"),      &HandleST_RemoveTransition);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetStateTreeCapability)

#endif // WITH_STATETREE
