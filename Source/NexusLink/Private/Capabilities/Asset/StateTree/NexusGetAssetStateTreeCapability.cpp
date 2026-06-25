// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusGetAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "StateTree.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#endif

// ── 辅助：枚举 EStateTreeStateType → 字符串 ──────────────────────────────────

#if WITH_EDITOR
static FString StateTypeToStr(EStateTreeStateType Type)
{
	switch (Type)
	{
		case EStateTreeStateType::State:       return TEXT("State");
		case EStateTreeStateType::Group:       return TEXT("Group");
		case EStateTreeStateType::Linked:      return TEXT("Linked");
		case EStateTreeStateType::Subtree:     return TEXT("Subtree");
		default:                               return TEXT("Unknown");
	}
}

// ── 辅助：从 FStateTreeEditorNode 数组生成节点摘要列表 ────────────────────────

static TArray<TSharedPtr<FJsonValue>> BuildEditorNodeArray(const TArray<FStateTreeEditorNode>& Nodes)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const FStateTreeEditorNode& EdNode : Nodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		// Node.Node 是 FInstancedStruct，取 ScriptStruct 获取类型名
		if (const UScriptStruct* NodeStruct = EdNode.Node.GetScriptStruct())
		{
			NodeObj->SetStringField(TEXT("nodeType"), NodeStruct->GetName());
		}
		// Node.Instance 是实例数据的 FInstancedStruct
		if (const UScriptStruct* InstStruct = EdNode.Instance.GetScriptStruct())
		{
			NodeObj->SetStringField(TEXT("instanceType"), InstStruct->GetName());
		}
		// 节点名（FStateTreeEditorNode 本身没有名字字段，由 UScriptStruct 提供）
		NodeObj->SetStringField(TEXT("id"), EdNode.ID.ToString());
		Result.Add(MakeShared<FJsonValueObject>(NodeObj));
	}
	return Result;
}

// ── 辅助：递归构建 UStateTreeState JSON ──────────────────────────────────────

static TSharedPtr<FJsonObject> BuildStateInfo(const UStateTreeState* State, int32 Depth = 0)
{
	if (!State || Depth > 32)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), State->Name.ToString());
	Obj->SetStringField(TEXT("type"), StateTypeToStr(State->Type));
	Obj->SetStringField(TEXT("id"), State->ID.ToString());

	// 进入条件节点数量
	if (State->EnterConditions.Num() > 0)
	{
		Obj->SetNumberField(TEXT("enterConditionsCount"), State->EnterConditions.Num());
		Obj->SetArrayField(TEXT("enterConditions"), BuildEditorNodeArray(State->EnterConditions));
	}

	// 任务节点
	if (State->Tasks.Num() > 0)
	{
		Obj->SetNumberField(TEXT("tasksCount"), State->Tasks.Num());
		Obj->SetArrayField(TEXT("tasks"), BuildEditorNodeArray(State->Tasks));
	}

	// 迁移摘要（迁移数量 + 触发器 + 目标名）
	if (State->Transitions.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TransArr;
		for (const FStateTreeTransition& T : State->Transitions)
		{
			TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
			// 目标状态名：若 State 指针有效取名字，否则记录类型
			if (T.State.IsValid())
			{
				TObj->SetStringField(TEXT("target"), T.State->Name.ToString());
			}
			TransArr.Add(MakeShared<FJsonValueObject>(TObj));
		}
		Obj->SetArrayField(TEXT("transitions"), TransArr);
	}

	// 子状态（递归）
	if (State->Children.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArr;
		for (const UStateTreeState* Child : State->Children)
		{
			TSharedPtr<FJsonObject> ChildObj = BuildStateInfo(Child, Depth + 1);
			if (ChildObj.IsValid())
			{
				ChildArr.Add(MakeShared<FJsonValueObject>(ChildObj));
			}
		}
		Obj->SetArrayField(TEXT("children"), ChildArr);
	}

	return Obj;
}
#endif // WITH_EDITOR

// ── Capability ────────────────────────────────────────────────────────────────

void FGetAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_state_tree");
	Out.Description = TEXT("检查 StateTree 结构快照。Schema/States 树/Evaluators/参数；只读。UE 5.5+。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("StateTree 资产路径（/Game/…/ST_Foo）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("state_tree"), TEXT("st"), TEXT("states"), TEXT("tasks"), TEXT("transitions"), TEXT("evaluators") };
	Out.RelatedCapabilities = { TEXT("search_asset"), TEXT("get_asset_behavior_tree"), TEXT("get_asset_refs"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读 StateTree 结构：Schema/States/Evaluators/迁移/条件/参数");
}

FCapabilityResult FGetAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

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

		OutEntry->SetStringField(TEXT("assetType"), TEXT("StateTree"));
		OutEntry->SetStringField(TEXT("name"), ST->GetName());
		OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(ST));
		OutEntry->SetBoolField(TEXT("isReadyToRun"), ST->IsReadyToRun());

		// Schema 信息
		if (const UStateTreeSchema* Schema = ST->GetSchema())
		{
			OutEntry->SetStringField(TEXT("schema"), Schema->GetClass()->GetName());
		}

#if WITH_EDITOR
		// 编辑器数据（StateTree::EditorData 为 TObjectPtr<UObject>，需 Cast）
		if (UStateTreeEditorData* EdData = Cast<UStateTreeEditorData>(ST->EditorData))
		{
			// 全局 Evaluators
			if (EdData->Evaluators.Num() > 0)
			{
				OutEntry->SetNumberField(TEXT("evaluatorsCount"), EdData->Evaluators.Num());
				OutEntry->SetArrayField(TEXT("evaluators"), BuildEditorNodeArray(EdData->Evaluators));
			}

#if NX_UE_HAS_STATETREE_GLOBAL_TASKS
			// UE 5.5+ GlobalTasks
			if (EdData->GlobalTasks.Num() > 0)
			{
				OutEntry->SetNumberField(TEXT("globalTasksCount"), EdData->GlobalTasks.Num());
				OutEntry->SetArrayField(TEXT("globalTasks"), BuildEditorNodeArray(EdData->GlobalTasks));
			}
#endif

			// 根参数摘要（FInstancedPropertyBag：只记属性数量）
			const FInstancedPropertyBag& Params = ST->GetDefaultParameters();
			if (const UPropertyBag* BagStruct = Params.GetPropertyBagStruct())
			{
				int32 PropCount = 0;
				for (TFieldIterator<FProperty> It(BagStruct); It; ++It)
				{
					++PropCount;
				}
				OutEntry->SetNumberField(TEXT("parametersCount"), PropCount);
			}

			// SubTrees（根状态树递归）
			TArray<TSharedPtr<FJsonValue>> SubTreeArr;
			for (const UStateTreeState* SubTree : EdData->SubTrees)
			{
				TSharedPtr<FJsonObject> SubObj = BuildStateInfo(SubTree);
				if (SubObj.IsValid())
				{
					SubTreeArr.Add(MakeShared<FJsonValueObject>(SubObj));
				}
			}
			OutEntry->SetArrayField(TEXT("subTrees"), SubTreeArr);
		}
#else
		OutEntry->SetStringField(TEXT("note"), TEXT("编辑器数据仅在 WITH_EDITOR 构建下可用"));
#endif // WITH_EDITOR

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetStateTreeCapability)

#endif // WITH_STATETREE
