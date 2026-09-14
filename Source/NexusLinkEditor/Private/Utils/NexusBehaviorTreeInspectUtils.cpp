// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusBehaviorTreeInspectUtils.h"
#include "Utils/NexusPropertyReportUtils.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"

/** 将 UObject 可编辑 UPROPERTY 序列化为 JSON 数组（不分页，带上限）。
 *  SubobjectDepth=2：递归展开 Task/Decorator 内层 instanced 子对象属性至 subProperties。*/
static TArray<TSharedPtr<FJsonValue>> BuildEditableProps(const UObject* Obj, int32 MaxCount = 64)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!Obj)
	{
		return Result;
	}

	int32 Total = 0;
	Result = FNexusPropertyReportUtils::BuildEditablePropsPage(
		Obj->GetClass(),
		const_cast<UObject*>(Obj),
		Obj->GetClass(),
		TEXT(""),
		TArray<FString>(),
		0,
		MaxCount,
		Total,
		/*SubobjectDepth=*/2,
		/*SubobjectMaxCount=*/16);
	return Result;
}

/** 由父路径与槽位下标拼出节点点分路径（与 manage_asset_behavior_tree 一致）。 */
static FString BuildChildPath(const FString& ParentPath, int32 ChildIndex)
{
	return ParentPath.IsEmpty()
		? FString::FromInt(ChildIndex)
		: ParentPath + TEXT(".") + FString::FromInt(ChildIndex);
}

TSharedPtr<FJsonObject> FNexusBehaviorTreeInspectUtils::BuildBTNodeInfo(const UBTNode* Node, const FString& Path)
{
	// 初始化全局先序扁平序号计数器，根节点分配 0
	int32 FlatCounter = 0;
	return BuildBTNodeInfoRec(Node, Path, FlatCounter);
}

TSharedPtr<FJsonObject> FNexusBehaviorTreeInspectUtils::BuildBTNodeInfoRec(const UBTNode* Node, const FString& Path, int32& FlatCounter)
{
	if (!Node)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
	NodeObj->SetStringField(TEXT("name"), Node->GetNodeName());
	NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("path"), Path);
	// 先序 DFS：进入本节点时分配序号，与 manage_asset_behavior_tree 的路径定位互补
	NodeObj->SetNumberField(TEXT("flatIndex"), FlatCounter++);

	TArray<TSharedPtr<FJsonValue>> NodeProps = BuildEditableProps(Node);
	if (NodeProps.Num() > 0)
	{
		NodeObj->SetArrayField(TEXT("properties"), NodeProps);
	}

	if (const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
	{
		TArray<TSharedPtr<FJsonValue>> Services;
		for (int32 si = 0; si < Composite->Services.Num() && si < 32; ++si)
		{
			const UBTService* Svc = Composite->Services[si];
			if (!Svc)
			{
				continue;
			}
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetNumberField(TEXT("index"), si);
			S->SetStringField(TEXT("class"), Svc->GetClass()->GetName());
			S->SetStringField(TEXT("name"), Svc->GetNodeName());
			S->SetStringField(TEXT("parentPath"), Path);
			TArray<TSharedPtr<FJsonValue>> SvcProps = BuildEditableProps(Svc);
			if (SvcProps.Num() > 0)
			{
				S->SetArrayField(TEXT("properties"), SvcProps);
			}
			Services.Add(MakeShared<FJsonValueObject>(S));
		}
		if (Services.Num() > 0)
		{
			NodeObj->SetArrayField(TEXT("services"), Services);
		}

		TArray<TSharedPtr<FJsonValue>> Children;
		for (int32 i = 0; i < Composite->Children.Num() && i < 64; ++i)
		{
			const UBTNode* Child = Composite->Children[i].ChildComposite
				? static_cast<const UBTNode*>(Composite->Children[i].ChildComposite)
				: static_cast<const UBTNode*>(Composite->Children[i].ChildTask);
			if (!Child)
			{
				continue;
			}

			const FString ChildPath = BuildChildPath(Path, i);
			// 递归时传入计数器引用，保证全树先序连续
			TSharedPtr<FJsonObject> ChildObj = BuildBTNodeInfoRec(Child, ChildPath, FlatCounter);
			if (!ChildObj.IsValid())
			{
				continue;
			}
			ChildObj->SetNumberField(TEXT("childIndex"), i);

			TArray<TSharedPtr<FJsonValue>> Decorators;
			for (int32 di = 0; di < Composite->Children[i].Decorators.Num() && di < 32; ++di)
			{
				const UBTDecorator* Dec = Composite->Children[i].Decorators[di];
				if (!Dec)
				{
					continue;
				}
				TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
				D->SetNumberField(TEXT("index"), di);
				D->SetNumberField(TEXT("childIndex"), i);
				D->SetStringField(TEXT("parentPath"), Path);
				D->SetStringField(TEXT("class"), Dec->GetClass()->GetName());
				D->SetStringField(TEXT("name"), Dec->GetNodeName());
				TArray<TSharedPtr<FJsonValue>> DecProps = BuildEditableProps(Dec);
				if (DecProps.Num() > 0)
				{
					D->SetArrayField(TEXT("properties"), DecProps);
				}
				Decorators.Add(MakeShared<FJsonValueObject>(D));
			}
			if (Decorators.Num() > 0)
			{
				ChildObj->SetArrayField(TEXT("decorators"), Decorators);
			}
			Children.Add(MakeShared<FJsonValueObject>(ChildObj));
		}
		if (Children.Num() > 0)
		{
			NodeObj->SetArrayField(TEXT("children"), Children);
		}
		NodeObj->SetStringField(TEXT("composite"), TEXT("true"));
	}
	return NodeObj;
}
