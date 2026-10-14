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

	UStateTreeState* NewState = NewObject<UStateTreeState>(EdData, NAME_None, RF_Transactional);
	NewState->Name = *StateName;
	NewState->Type = StateType;

	const FString ParentName = A.Str(TEXT("ParentState"));
	if (!ParentName.IsEmpty())
	{
		UStateTreeState* ParentState = FindStateByName(EdData, ParentName);
		if (!ParentState)
		{
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Parent State not found: %s"), *ParentName));
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
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State not found: %s"), *StateName));
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
	else Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveState failed"));
}

static void HandleST_RenameState(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const FString NewName = A.Str(TEXT("NewName"));
	if (StateName.IsEmpty() || NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RenameState requires StateName and NewName"));
		return;
	}
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("State not found: %s"), *StateName));
		return;
	}
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
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const FString NodeType = A.Str(TEXT("NodeType"));
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target || NodeType.IsEmpty())
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
	FStateTreeEditorNode NewNode;
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(Struct);
	if (bTask)
	{
		Target->Tasks.Add(NewNode);
		Ctx.Entry->SetNumberField(TEXT("TasksCount"), Target->Tasks.Num());
	}
	else
	{
		Target->EnterConditions.Add(NewNode);
		Ctx.Entry->SetNumberField(TEXT("EnterConditionsCount"), Target->EnterConditions.Num());
	}
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
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const int32 Idx = Op->HasField(TEXT("Index")) ? static_cast<int32>(A.Num(TEXT("Index"))) : 0;
	UStateTreeState* Target = FindStateByName(EdData, StateName);
	if (!Target)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Valid StateName required"));
		return;
	}
	TArray<FStateTreeEditorNode>& Arr = bTask ? Target->Tasks : Target->EnterConditions;
	if (!Arr.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("index out of bounds"));
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
	const FString StateName = A.Str(TEXT("StateName"));
	const FString TargetName = A.Str(TEXT("TargetState"));
	UStateTreeState* Source = FindStateByName(EdData, StateName);
	UStateTreeState* Dest = FindStateByName(EdData, TargetName);
	if (!Source || !Dest)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTransition requires valid StateName and TargetState"));
		return;
	}
	FStateTreeTransition Trans;
	Trans.Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	Trans.State.ID = Dest->ID;
	Source->Transitions.Add(Trans);
	Ctx.Entry->SetStringField(TEXT("TargetState"), TargetName);
	MarkSTDirty(Ctx);
}

static void HandleST_RemoveTransition(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStateTreeEditorData* EdData = EdFrom(Ctx);
	const FNexusArgs A(Op);
	const FString StateName = A.Str(TEXT("StateName"));
	const int32 Idx = Op->HasField(TEXT("Index")) ? static_cast<int32>(A.Num(TEXT("Index"))) : 0;
	UStateTreeState* Source = FindStateByName(EdData, StateName);
	if (!Source || !Source->Transitions.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTransition requires valid StateName and index"));
		return;
	}
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
