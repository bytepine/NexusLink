// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusGetAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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
			NodeObj->SetStringField(TEXT("NodeType"), NodeStruct->GetName());
		}
		// Node.Instance 是实例数据的 FInstancedStruct
		if (const UScriptStruct* InstStruct = EdNode.Instance.GetScriptStruct())
		{
			NodeObj->SetStringField(TEXT("InstanceType"), InstStruct->GetName());
		}
		// 节点名（FStateTreeEditorNode 本身没有名字字段，由 UScriptStruct 提供）
		NodeObj->SetStringField(TEXT("Id"), EdNode.ID.ToString());
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
	Obj->SetStringField(TEXT("Id"), State->ID.ToString());

	// 进入条件节点数量
	if (State->EnterConditions.Num() > 0)
	{
		Obj->SetNumberField(TEXT("EnterConditionsCount"), State->EnterConditions.Num());
		Obj->SetArrayField(TEXT("EnterConditions"), BuildEditorNodeArray(State->EnterConditions));
	}

	// 任务节点
	if (State->Tasks.Num() > 0)
	{
		Obj->SetNumberField(TEXT("TasksCount"), State->Tasks.Num());
		Obj->SetArrayField(TEXT("Tasks"), BuildEditorNodeArray(State->Tasks));
	}

	// 迁移摘要（迁移数量 + 触发器 + 目标名）
	if (State->Transitions.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TransArr;
		for (const FStateTreeTransition& T : State->Transitions)
		{
			TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
			// 目标状态名：若 State 指针有效取名字，否则记录类型
			TObj->SetStringField(TEXT("Trigger"), UEnum::GetValueAsString(T.Trigger));
#if WITH_EDITORONLY_DATA
			if (T.State.ID.IsValid())
			{
				TObj->SetStringField(TEXT("Target"), T.State.Name.ToString());
			}
#endif
			TransArr.Add(MakeShared<FJsonValueObject>(TObj));
		}
		Obj->SetArrayField(TEXT("Transitions"), TransArr);
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
		Obj->SetArrayField(TEXT("Children"), ChildArr);
	}

	return Obj;
}
#endif // WITH_EDITOR

// ── Capability ────────────────────────────────────────────────────────────────

void FNexusGetAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetStateTree");
	Out.SearchAssetTypes = {TEXT("StateTree")};
	Out.Description = TEXT("Inspect StateTree structure. Read-only. UE 5.5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("StateTree asset path (/Game/…/ST_Foo)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("StateTree"), TEXT("st"), TEXT("States"), TEXT("Tasks"), TEXT("Transitions"), TEXT("Evaluators") };
	Out.RelatedCapabilities = { TEXT("ManageAssetStateTree"), TEXT("SearchAsset"), TEXT("GetAssetBehaviorTree"), TEXT("GetAssetRefs"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Read StateTree structure: Schema/States/Evaluators/transitions/conditions/params");
}

FCapabilityResult FNexusGetAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UStateTree* ST = FNexusAssetUtils::LoadAssetWithFallback<UStateTree>(AssetPath);
		if (!ST)
		{
			OutError = FString::Printf(TEXT("StateTree not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"), TEXT("StateTree"));
		OutEntry->SetStringField(TEXT("name"), ST->GetName());
		OutEntry->SetStringField(TEXT("Path"), FNexusAssetUtils::PackagePathOf(ST));
		OutEntry->SetBoolField(TEXT("bIsReadyToRun"), ST->IsReadyToRun());

		// Schema 信息
		if (const UStateTreeSchema* Schema = ST->GetSchema())
		{
			OutEntry->SetStringField(TEXT("Schema"), Schema->GetClass()->GetName());
		}

#if WITH_EDITOR
		// 编辑器数据（StateTree::EditorData 为 TObjectPtr<UObject>，需 Cast）
		if (UStateTreeEditorData* EdData = Cast<UStateTreeEditorData>(ST->EditorData))
		{
			// 全局 Evaluators
			if (EdData->Evaluators.Num() > 0)
			{
				OutEntry->SetNumberField(TEXT("EvaluatorsCount"), EdData->Evaluators.Num());
				OutEntry->SetArrayField(TEXT("Evaluators"), BuildEditorNodeArray(EdData->Evaluators));
			}

#if NX_UE_HAS_STATETREE_GLOBAL_TASKS
			// UE 5.5+ GlobalTasks
			if (EdData->GlobalTasks.Num() > 0)
			{
				OutEntry->SetNumberField(TEXT("GlobalTasksCount"), EdData->GlobalTasks.Num());
				OutEntry->SetArrayField(TEXT("GlobalTasks"), BuildEditorNodeArray(EdData->GlobalTasks));
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
				OutEntry->SetNumberField(TEXT("ParametersCount"), PropCount);
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
			OutEntry->SetArrayField(TEXT("SubTrees"), SubTreeArr);
		}
#else
		OutEntry->SetStringField(TEXT("Note"), TEXT("Editor data only available in WITH_EDITOR builds"));
#endif // WITH_EDITOR

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetStateTreeCapability)

#endif // WITH_STATETREE
