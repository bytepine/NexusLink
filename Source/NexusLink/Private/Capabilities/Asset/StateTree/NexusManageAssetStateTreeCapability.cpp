// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusManageAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("编辑 StateTree：add_state/remove_state/rename_state/recompile。UE 5.5+。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{ TEXT("add_state"), TEXT("remove_state"), TEXT("rename_state"), TEXT("recompile") }))
		.Prop(TEXT("stateName"),    FNexusSchema::Str(TEXT("目标 State 名（add/rename/remove 时必填）")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("rename_state：新名称")))
		.Prop(TEXT("parentState"),  FNexusSchema::Str(TEXT("add_state：父 State 名（空=顶层 SubTree）")))
		.Prop(TEXT("stateType"),    FNexusSchema::Enum(
			TEXT("add_state：状态类型"),
			{ TEXT("State"), TEXT("Group"), TEXT("Linked"), TEXT("Subtree") },
			TEXT("State")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("StateTree 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("state"), TEXT("st"), TEXT("task"), TEXT("transition"), TEXT("npc"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("get_asset_state_tree"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("增删改 StateTree 的 State 节点，或触发重编译");
}

FCapabilityResult FManageAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_state_tree 仅在编辑器构建可用");
		return;
#else
		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UStateTree* ST = FNexusAssetUtils::LoadAssetWithFallback<UStateTree>(AssetPath);
		if (!ST)
		{
			OutError = FString::Printf(TEXT("StateTree 未找到: %s"), *AssetPath);
			return;
		}

		UStateTreeEditorData* EdData = Cast<UStateTreeEditorData>(ST->EditorData);
		if (!EdData)
		{
			OutError = TEXT("StateTree 无编辑器数据，无法编辑（仅限编辑器构建）");
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Ops;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
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

		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
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
					OpResult->SetStringField(TEXT("error"), TEXT("add_state 需要 stateName"));
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
							OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("父 State 未找到: %s"), *ParentName));
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
					OpResult->SetStringField(TEXT("error"), TEXT("remove_state 需要 stateName"));
				}
				else
				{
					UStateTreeState* Target = FindStateByName(StateName);
					if (!Target)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到 State: %s"), *StateName));
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
						else OpResult->SetStringField(TEXT("error"), TEXT("remove_state 失败"));
					}
				}
			}
			else if (Action == TEXT("rename_state"))
			{
				FString StateName, NewName;
				if (!Op->TryGetStringField(TEXT("stateName"), StateName) || !Op->TryGetStringField(TEXT("newName"), NewName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("rename_state 需要 stateName 和 newName"));
				}
				else
				{
					UStateTreeState* Target = FindStateByName(StateName);
					if (!Target)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到 State: %s"), *StateName));
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
				// 触发 StateTree 重编译（标记脏，编辑器在保存时重编）
				ST->MarkPackageDirty();
				OpResult->SetStringField(TEXT("note"), TEXT("已标记为脏；关闭/重新打开编辑器或调用 save_asset 后触发重编译"));
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
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
