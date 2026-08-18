// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusManageAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_state_tree only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("assetPath"));

		UStateTree* ST = FNexusAssetUtils::LoadAssetWithFallback<UStateTree>(AssetPath);
		if (!ST)
		{
			OutError = FString::Printf(TEXT("StateTree not found: %s"), *AssetPath);
			return;
		}

		UStateTreeEditorData* EdData = Cast<UStateTreeEditorData>(ST->EditorData);
		if (!EdData)
		{
			OutError = TEXT("StateTree has no editor data; editor builds only");
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

		bool bDirty = false;

		// 辅助：按名字在 SubTrees 中递归查找 UStateTreeState
		auto FindStateByName = [&](const FString& Name) -> UStateTreeState*
		{
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
		};

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_state"))
			{
				FString StateName;
				if (!Op->TryGetStringField(TEXT("stateName"), StateName) || StateName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_state requires stateName"));
				}
				else
				{
					// 解析 stateType
					EStateTreeStateType StateType = EStateTreeStateType::State;
					FString TypeStr;
					if (Op->TryGetStringField(TEXT("stateType"), TypeStr))
					{
						if (TypeStr == TEXT("Group"))       StateType = EStateTreeStateType::Group;
						else if (TypeStr == TEXT("Linked"))  StateType = EStateTreeStateType::Linked;
						else if (TypeStr == TEXT("Subtree")) StateType = EStateTreeStateType::Subtree;
					}

					UStateTreeState* NewState = NewObject<UStateTreeState>(EdData, NAME_None, RF_Transactional);
					NewState->Name = *StateName;
					NewState->Type = StateType;

					// 确定父节点
					FString ParentName;
					bool bHasParent = Op->TryGetStringField(TEXT("parentState"), ParentName) && !ParentName.IsEmpty();
					if (bHasParent)
					{
						UStateTreeState* ParentState = FindStateByName(ParentName);
						if (!ParentState)
						{
							OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent State not found: %s"), *ParentName));
							OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
							continue;
						}
						NewState->Rename(nullptr, ParentState);
						ParentState->Children.Add(NewState);
					}
					else
					{
						// 添加到 SubTrees（顶层）
						NewState->Rename(nullptr, EdData);
						EdData->SubTrees.Add(NewState);
					}

					OpResult->SetStringField(TEXT("addedState"), StateName);
					bDirty = true;
				}
			}
			else if (Action == TEXT("remove_state"))
			{
				FString StateName;
				if (!Op->TryGetStringField(TEXT("stateName"), StateName) || StateName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_state requires stateName"));
				}
				else
				{
					UStateTreeState* Target = FindStateByName(StateName);
					if (!Target)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("State not found: %s"), *StateName));
					}
					else
					{
						// 从父节点或 SubTrees 移除
						bool bRemoved = false;
						if (EdData->SubTrees.Remove(Target) > 0)
						{
							bRemoved = true;
						}
						else
						{
							// 遍历所有 Children
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
						if (bRemoved) bDirty = true;
						else OpResult->SetStringField(TEXT("error"), TEXT("remove_state failed"));
					}
				}
			}
			else if (Action == TEXT("rename_state"))
			{
				FString StateName, NewName;
				if (!Op->TryGetStringField(TEXT("stateName"), StateName) || !Op->TryGetStringField(TEXT("newName"), NewName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("rename_state requires stateName and newName"));
				}
				else
				{
					UStateTreeState* Target = FindStateByName(StateName);
					if (!Target)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("State not found: %s"), *StateName));
					}
					else
					{
						Target->Name = *NewName;
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("recompile"))
			{
				ST->MarkPackageDirty();
				OpResult->SetStringField(TEXT("note"), TEXT("Marked dirty; reopen editor or save_asset to recompile"));
			}
			else if (Action == TEXT("add_task") || Action == TEXT("add_enter_condition"))
			{
				FString StateName, NodeType;
				Op->TryGetStringField(TEXT("stateName"), StateName);
				Op->TryGetStringField(TEXT("nodeType"), NodeType);
				UStateTreeState* Target = FindStateByName(StateName);
				if (!Target || NodeType.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("stateName and nodeType required"));
				}
				else
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
					if (!Struct)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("struct not found: %s"), *NodeType));
					}
					else
					{
						FStateTreeEditorNode NewNode;
						NewNode.ID = FGuid::NewGuid();
						NewNode.Node.InitializeAs(Struct);
						if (Action == TEXT("add_task"))
						{
							Target->Tasks.Add(NewNode);
							OpResult->SetNumberField(TEXT("tasksCount"), Target->Tasks.Num());
						}
						else
						{
							Target->EnterConditions.Add(NewNode);
							OpResult->SetNumberField(TEXT("enterConditionsCount"), Target->EnterConditions.Num());
						}
						OpResult->SetStringField(TEXT("nodeType"), Struct->GetName());
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove_task") || Action == TEXT("remove_enter_condition"))
			{
				FString StateName;
				Op->TryGetStringField(TEXT("stateName"), StateName);
				int32 Idx = 0;
				if (Op->HasField(TEXT("index"))) Idx = static_cast<int32>(Op->GetNumberField(TEXT("index")));
				UStateTreeState* Target = FindStateByName(StateName);
				if (!Target)
				{
					OpResult->SetStringField(TEXT("error"), TEXT("Valid stateName required"));
				}
				else
				{
					TArray<FStateTreeEditorNode>& Arr = (Action == TEXT("remove_task")) ? Target->Tasks : Target->EnterConditions;
					if (!Arr.IsValidIndex(Idx))
					{
						OpResult->SetStringField(TEXT("error"), TEXT("index out of bounds"));
					}
					else
					{
						Arr.RemoveAt(Idx);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("add_transition"))
			{
				FString StateName, TargetName;
				Op->TryGetStringField(TEXT("stateName"), StateName);
				Op->TryGetStringField(TEXT("targetState"), TargetName);
				UStateTreeState* Source = FindStateByName(StateName);
				UStateTreeState* Dest = FindStateByName(TargetName);
				if (!Source || !Dest)
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_transition requires valid stateName and targetState"));
				}
				else
				{
					FStateTreeTransition Trans;
					Trans.Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
					Trans.State.ID = Dest->ID;
					Source->Transitions.Add(Trans);
					OpResult->SetStringField(TEXT("targetState"), TargetName);
					bDirty = true;
				}
			}
			else if (Action == TEXT("remove_transition"))
			{
				FString StateName;
				Op->TryGetStringField(TEXT("stateName"), StateName);
				int32 Idx = 0;
				if (Op->HasField(TEXT("index"))) Idx = static_cast<int32>(Op->GetNumberField(TEXT("index")));
				UStateTreeState* Source = FindStateByName(StateName);
				if (!Source || !Source->Transitions.IsValidIndex(Idx))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_transition requires valid stateName and index"));
				}
				else
				{
					Source->Transitions.RemoveAt(Idx);
					bDirty = true;
				}
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			ST->MarkPackageDirty();
		}
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetStateTreeCapability)

#endif // WITH_STATETREE
