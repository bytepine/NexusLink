// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetBehaviorTreeCapability.h"
#include "NexusActionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusBehaviorTreeEditUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTService.h"
#include "NexusMcpTool.h"

/** 把 ApplyInitialProperties 收集到的失败项写进本条 Entry 的 propertyErrors[] */
static void ReportPropertyErrors(const TSharedPtr<FJsonObject>& Entry, const TArray<FString>& Errors)
{
	if (!Entry.IsValid() || Errors.Num() == 0) return;
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& E : Errors) Arr.Add(MakeShared<FJsonValueString>(E));
	Entry->SetArrayField(TEXT("propertyErrors"), Arr);
}

// ─── Execute ─────────────────────────────────────────────────────────────────

void FManageAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_behavior_tree");
	Out.SearchAssetTypes = {TEXT("BehaviorTree")};
	// 详细语义（图同步边界、replace_node vs remove+add）见 docs/tool-reference.md，此处保持 ≤100 字符
	Out.Description = TEXT("Batch edit BT nodes/decorators/services. replace_node swaps type; sync_graph fixes graph drift.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),      FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("set_root"), TEXT("add_node"), TEXT("remove_node"), TEXT("replace_node"), TEXT("move_node"),
			  TEXT("add_decorator"), TEXT("remove_decorator"),
			  TEXT("add_service"),   TEXT("remove_service"),
			  TEXT("set_blackboard"), TEXT("set_property"), TEXT("sync_graph") }))
		.Prop(TEXT("nodeClass"),   FNexusSchema::Str(TEXT("Node class (set_root/add_node/replace_node/add_decorator/add_service)")))
		.Prop(TEXT("nodeName"),    FNexusSchema::Str(TEXT("Display name override (optional)")))
		.Prop(TEXT("parentPath"),  FNexusSchema::Str(TEXT("Child index path from root, e.g. '' or '0.1'")))
		.Prop(TEXT("childIndex"),  FNexusSchema::Int(TEXT("Child slot index (add_node/move_node/decorator/service)"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("targetPath"),  FNexusSchema::Str(TEXT("Target node dot path (remove_node/replace_node/move_node/set_property)")))
		.Prop(TEXT("targetIndex"), FNexusSchema::Int(TEXT("Index in decorators[]/services[] to edit/remove"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("blackboardPath"), FNexusSchema::Str(TEXT("BlackboardData asset path (set_blackboard)")))
		.Prop(TEXT("targetType"),  FNexusSchema::Enum(TEXT("Target type for set_property"),
			{ TEXT("node"), TEXT("decorator"), TEXT("service") }))
		.Prop(TEXT("propertyName"),  FNexusSchema::Str(TEXT("UPROPERTY name to set (set_property)")))
		.Prop(TEXT("propertyValue"), FNexusSchema::Str(TEXT("Text value, ImportText format (set_property)")))
		.Prop(TEXT("properties"),  FNexusSchema::ArrOfObj(TEXT("Initial props for add_node/replace_node [{name,value}]")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("BehaviorTree asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("bt"), TEXT("node"), TEXT("decorator"), TEXT("service"), TEXT("blackboard"),
		TEXT("replace"), TEXT("graph"), TEXT("sync")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_behavior_tree"), TEXT("manage_asset_blackboard"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/replace/move nodes, decorators, services, set props; sync_graph when graph/tree drift");
}

static void HandleBT_SetRoot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("set_root requires nodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class || !Class->IsChildOf(UBTCompositeNode::StaticClass()))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("nodeClass '%s' not found or not BTCompositeNode subclass"), *NodeClass));
			return;
		}

		UBTCompositeNode* NewRoot = NewObject<UBTCompositeNode>(BT, Class);
		FString NodeName;
		if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
		{
			NewRoot->NodeName = NodeName;
		}
		BT->RootNode = NewRoot;
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
}
static void HandleBT_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("add_node requires nodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("nodeClass '%s' not found"), *NodeClass));
			return;
		}
		const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
		const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
		if (!bIsComposite && !bIsTask)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("nodeClass '%s' must be BTCompositeNode or BTTaskNode subclass"), *NodeClass));
			return;
		}

		FString ParentPath;
		Op->TryGetStringField(TEXT("parentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		FBTCompositeChild NewChild;
		NewChild.ChildComposite = nullptr;
		NewChild.ChildTask = nullptr;
		UBTNode* CreatedNode = nullptr;
		if (bIsComposite)
		{
			UBTCompositeNode* NewComp = NewObject<UBTCompositeNode>(BT, Class);
			FString NodeName;
			if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewComp->NodeName = NodeName;
			}
			NewChild.ChildComposite = NewComp;
			CreatedNode = NewComp;
		}
		else
		{
			UBTTaskNode* NewTask = NewObject<UBTTaskNode>(BT, Class);
			FString NodeName;
			if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewTask->NodeName = NodeName;
			}
			NewChild.ChildTask = NewTask;
			CreatedNode = NewTask;
		}

		// 支持在创建节点时直接设置初始属性（避免后续 set_property 因类卸载而崩溃）
		TArray<FString> PropErrors;
		FNexusBehaviorTreeEditUtils::ApplyInitialProperties(CreatedNode, Op, PropErrors);
		ReportPropertyErrors(Entry, PropErrors);

		const int32 InsertIdx = [&]() -> int32
		{
			if (Op->HasField(TEXT("childIndex")))
			{
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("childIndex")));
				return FMath::Clamp(Idx, 0, Parent->Children.Num());
			}
			return Parent->Children.Num();
		}();
		Parent->Children.Insert(NewChild, InsertIdx);
		BT->MarkPackageDirty();

		const FString AddedPath = ParentPath.IsEmpty()
			? FString::FromInt(InsertIdx)
			: ParentPath + TEXT(".") + FString::FromInt(InsertIdx);
		Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
		Entry->SetStringField(TEXT("addedPath"), AddedPath);
		Entry->SetNumberField(TEXT("childIndex"), static_cast<double>(InsertIdx));
}
static void HandleBT_MoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString TargetPath;
		if (!Op->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("move_node requires targetPath"));
			return;
		}

		UBTCompositeNode* SrcParent = nullptr;
		int32 SrcIdx = INDEX_NONE;
		if (!FNexusBehaviorTreeEditUtils::FindParentAndIndex(BT, TargetPath, SrcParent, SrcIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid targetPath '%s'"), *TargetPath));
			return;
		}

		FString NewParentPath;
		Op->TryGetStringField(TEXT("parentPath"), NewParentPath);
		if (NewParentPath.StartsWith(TargetPath + TEXT(".")) || NewParentPath == TargetPath)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Cannot move node into itself or its subtree"));
			return;
		}

		UBTCompositeNode* DstParent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, NewParentPath);
		if (!DstParent)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("parentPath '%s' is not a composite node or does not exist"), *NewParentPath));
			return;
		}

		const FBTCompositeChild MovedChild = SrcParent->Children[SrcIdx];
		SrcParent->Children.RemoveAt(SrcIdx);

		const int32 InsertIdx = [&]() -> int32
		{
			if (Op->HasField(TEXT("childIndex")))
			{
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("childIndex")));
				return FMath::Clamp(Idx, 0, DstParent->Children.Num());
			}
			return DstParent->Children.Num();
		}();
		DstParent->Children.Insert(MovedChild, InsertIdx);
		BT->MarkPackageDirty();

		const FString NewPath = NewParentPath.IsEmpty()
			? FString::FromInt(InsertIdx)
			: NewParentPath + TEXT(".") + FString::FromInt(InsertIdx);
		Entry->SetStringField(TEXT("movedPath"), NewPath);
		Entry->SetNumberField(TEXT("childIndex"), static_cast<double>(InsertIdx));
}
static void HandleBT_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString TargetPath;
		if (!Op->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("remove_node requires targetPath; use set_root to replace root"));
			return;
		}

		UBTCompositeNode* Parent = nullptr;
		int32 ChildIdx           = INDEX_NONE;
		if (!FNexusBehaviorTreeEditUtils::FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid targetPath '%s'"), *TargetPath));
			return;
		}

		Parent->Children.RemoveAt(ChildIdx);
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("removedPath"), TargetPath);
}
static void HandleBT_ReplaceNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	// 原子操作：就地替换某个已有子节点的类型（decorators/services 保留在同槽位）。
		FString TargetPath;
		if (!Op->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("replace_node requires targetPath; use set_root to replace root"));
			return;
		}

		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("replace_node requires nodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("nodeClass '%s' not found"), *NodeClass));
			return;
		}
		const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
		const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
		if (!bIsComposite && !bIsTask)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("nodeClass '%s' must be BTCompositeNode or BTTaskNode subclass"), *NodeClass));
			return;
		}

		UBTCompositeNode* Parent = nullptr;
		int32 ChildIdx           = INDEX_NONE;
		if (!FNexusBehaviorTreeEditUtils::FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid targetPath '%s'"), *TargetPath));
			return;
		}

		FBTCompositeChild& Slot = Parent->Children[ChildIdx];
		const bool bOldIsComposite = Slot.ChildComposite != nullptr;
		if (bOldIsComposite != bIsComposite)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Node type mismatch: original is %s, new class is %s; both must be Composite or both Task"),
				bOldIsComposite ? TEXT("Composite") : TEXT("Task"),
				bIsComposite ? TEXT("Composite") : TEXT("Task")));
			return;
		}

		UBTNode* OldNode = Slot.ChildComposite
			? static_cast<UBTNode*>(Slot.ChildComposite)
			: static_cast<UBTNode*>(Slot.ChildTask);
		const FString OldNodeClassName = (OldNode && OldNode->GetClass()) ? OldNode->GetClass()->GetName() : TEXT("");

		UBTNode* CreatedNode = nullptr;
		int32 MovedChildren = 0;
		int32 MovedServices = 0;
		if (bIsComposite)
		{
			UBTCompositeNode* NewComp = NewObject<UBTCompositeNode>(BT, Class);
			FString NodeName;
			if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewComp->NodeName = NodeName;
			}
			// 子树与 composite 自身的服务挂在旧节点上，不迁移就会随旧节点一起丢掉
			if (UBTCompositeNode* OldComp = Cast<UBTCompositeNode>(OldNode))
			{
				NewComp->Children = OldComp->Children;
				NewComp->Services = OldComp->Services;
				MovedChildren = NewComp->Children.Num();
				MovedServices = NewComp->Services.Num();
			}
			Slot.ChildComposite = NewComp;
			CreatedNode = NewComp;
		}
		else
		{
			UBTTaskNode* NewTask = NewObject<UBTTaskNode>(BT, Class);
			FString NodeName;
			if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewTask->NodeName = NodeName;
			}
			Slot.ChildTask = NewTask;
			CreatedNode = NewTask;
		}

		TArray<FString> PropErrors;
		FNexusBehaviorTreeEditUtils::ApplyInitialProperties(CreatedNode, Op, PropErrors);
		ReportPropertyErrors(Entry, PropErrors);
		BT->MarkPackageDirty();

#if WITH_EDITOR
		const bool bGraphSynced = FNexusBehaviorTreeEditUtils::SyncEdGraphNodeInstance(BT, OldNode, CreatedNode);
		Entry->SetBoolField(TEXT("graphSynced"), bGraphSynced);
#endif

		Entry->SetStringField(TEXT("replacedPath"),   TargetPath);
		Entry->SetStringField(TEXT("oldNodeClass"),   OldNodeClassName);
		Entry->SetStringField(TEXT("nodeClass"),      Class->GetName());
		if (MovedChildren > 0) Entry->SetNumberField(TEXT("movedChildren"), MovedChildren);
		if (MovedServices > 0) Entry->SetNumberField(TEXT("movedServices"), MovedServices);
}
static void HandleBT_AddDecorator(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("add_decorator requires nodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class || !Class->IsChildOf(UBTDecorator::StaticClass()))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("nodeClass '%s' not found or not BTDecorator subclass"), *NodeClass));
			return;
		}

		FString ParentPath;
		Op->TryGetStringField(TEXT("parentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		int32 ChildIdx = 0;
		if (Op->HasField(TEXT("childIndex")))
		{
			ChildIdx = (int32)Op->GetNumberField(TEXT("childIndex"));
		}
		if (!Parent->Children.IsValidIndex(ChildIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("childIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
			return;
		}

		UBTDecorator* Dec = NewObject<UBTDecorator>(BT, Class);
		FString NodeName;
		if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
		{
			Dec->NodeName = NodeName;
		}
		const int32 AddedIdx = Parent->Children[ChildIdx].Decorators.Add(Dec);
		BT->MarkPackageDirty();

		Entry->SetStringField(TEXT("nodeClass"),   Class->GetName());
		Entry->SetStringField(TEXT("parentPath"),  ParentPath);
		Entry->SetNumberField(TEXT("childIndex"),  ChildIdx);
		Entry->SetNumberField(TEXT("addedIndex"),  AddedIdx);
}
static void HandleBT_RemoveDecorator(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString ParentPath;
		Op->TryGetStringField(TEXT("parentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		int32 ChildIdx = 0;
		if (Op->HasField(TEXT("childIndex"))) ChildIdx = (int32)Op->GetNumberField(TEXT("childIndex"));
		if (!Parent->Children.IsValidIndex(ChildIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("childIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
			return;
		}

		int32 TargetIdx = 0;
		if (Op->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("targetIndex"));
		auto& Decs = Parent->Children[ChildIdx].Decorators;
		if (!Decs.IsValidIndex(TargetIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("targetIndex %d out of range [0, %d)"), TargetIdx, Decs.Num()));
			return;
		}

		Decs.RemoveAt(TargetIdx);
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("parentPath"), ParentPath);
		Entry->SetNumberField(TEXT("childIndex"), ChildIdx);
		Entry->SetNumberField(TEXT("removedIndex"), TargetIdx);
}
static void HandleBT_AddService(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("add_service requires nodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class || !Class->IsChildOf(UBTService::StaticClass()))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("nodeClass '%s' not found or not BTService subclass"), *NodeClass));
			return;
		}

		FString ParentPath;
		Op->TryGetStringField(TEXT("parentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		UBTService* Svc = NewObject<UBTService>(BT, Class);
		FString NodeName;
		if (Op->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
		{
			Svc->NodeName = NodeName;
		}
		const int32 AddedIdx = Parent->Services.Add(Svc);
		BT->MarkPackageDirty();

		Entry->SetStringField(TEXT("nodeClass"),  Class->GetName());
		Entry->SetStringField(TEXT("parentPath"), ParentPath);
		Entry->SetNumberField(TEXT("addedIndex"), AddedIdx);
}
static void HandleBT_RemoveService(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString ParentPath;
		Op->TryGetStringField(TEXT("parentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		int32 TargetIdx = 0;
		if (Op->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("targetIndex"));
		if (!Parent->Services.IsValidIndex(TargetIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("targetIndex %d out of range [0, %d)"), TargetIdx, Parent->Services.Num()));
			return;
		}

		Parent->Services.RemoveAt(TargetIdx);
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("parentPath"),   ParentPath);
		Entry->SetNumberField(TEXT("removedIndex"), TargetIdx);
}
static void HandleBT_SetBlackboard(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString BBPath;
		if (!Op->TryGetStringField(TEXT("blackboardPath"), BBPath) || BBPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("set_blackboard requires blackboardPath"));
			return;
		}

		UBlackboardData* BBAsset = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(BBPath);
		if (!BBAsset)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("BlackboardData not found: %s"), *BBPath));
			return;
		}

		BT->BlackboardAsset = BBAsset;
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("blackboardPath"), BBAsset->GetPathName());
}
static void HandleBT_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString PropertyName;
		if (!Op->TryGetStringField(TEXT("propertyName"), PropertyName) || PropertyName.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyName"));
			return;
		}

		FString PropertyValue;
		if (!Op->TryGetStringField(TEXT("propertyValue"), PropertyValue))
		{
			Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyValue"));
			return;
		}

		// 确定目标类型：node / decorator / service
		FString TargetType = TEXT("node");
		Op->TryGetStringField(TEXT("targetType"), TargetType);

		UBTNode* TargetNode = nullptr;

		if (TargetType == TEXT("node"))
		{
			FString TargetPath;
			Op->TryGetStringField(TEXT("targetPath"), TargetPath);
			TargetNode = FNexusBehaviorTreeEditUtils::FindNodeByPath(BT, TargetPath);
			if (!TargetNode)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Node not found at targetPath '%s'"), *TargetPath));
				return;
			}
		}
		else if (TargetType == TEXT("decorator"))
		{
			FString ParentPath;
			Op->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("parentPath '%s' not found"), *ParentPath));
				return;
			}

			int32 ChildIdx = 0;
			if (Op->HasField(TEXT("childIndex"))) ChildIdx = (int32)Op->GetNumberField(TEXT("childIndex"));
			if (!Parent->Children.IsValidIndex(ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("childIndex %d out of range"), ChildIdx));
				return;
			}

			int32 TargetIdx = 0;
			if (Op->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("targetIndex"));
			auto& Decs = Parent->Children[ChildIdx].Decorators;
			if (!Decs.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetIndex %d out of range"), TargetIdx));
				return;
			}
			TargetNode = Decs[TargetIdx];
		}
		else if (TargetType == TEXT("service"))
		{
			FString ParentPath;
			Op->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("parentPath '%s' not found"), *ParentPath));
				return;
			}

			int32 TargetIdx = 0;
			if (Op->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("targetIndex"));
			if (!Parent->Services.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetIndex %d out of range"), TargetIdx));
				return;
			}
			TargetNode = Parent->Services[TargetIdx];
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown targetType: '%s'"), *TargetType));
			return;
		}

		// 通过反射设置属性
		UClass* TargetNodeClass = TargetNode->GetClass();
		if (!TargetNodeClass)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Node '%s' GetClass() returned null (class may be unloaded)"), *TargetNode->GetName()));
			return;
		}
		FProperty* Prop = TargetNodeClass->FindPropertyByName(*PropertyName);
		if (!Prop)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' not found on %s"), *TargetNodeClass->GetName(), *PropertyName));
			return;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetNode);
#if NX_UE_HAS_IMPORT_TEXT_DIRECT
		const bool bImportOk = Prop->ImportText_Direct(*PropertyValue, ValuePtr, TargetNode, PPF_None) != nullptr;
#else
		const bool bImportOk = Prop->ImportText(*PropertyValue, ValuePtr, PPF_None, TargetNode) != nullptr;
#endif
		if (!bImportOk)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to set '%s' = '%s' (ImportText failed)"), *PropertyName, *PropertyValue));
			return;
		}

		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("targetType"),    TargetType);
		Entry->SetStringField(TEXT("propertyName"),  PropertyName);
		Entry->SetStringField(TEXT("propertyValue"), PropertyValue);
}
static void HandleBT_SyncGraph(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
#if WITH_EDITOR
		FString Msg;
		TArray<FString> Warnings;
		const bool bOk = FNexusBehaviorTreeEditUtils::RebuildGraphFromRootNode(BT, Msg, Warnings);
		Entry->SetBoolField(TEXT("graphSynced"), bOk);
		Entry->SetStringField(TEXT("message"), Msg);
		if (Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarnArr;
			for (const FString& W : Warnings) WarnArr.Add(MakeShared<FJsonValueString>(W));
			Entry->SetArrayField(TEXT("warnings"), WarnArr);
		}
#else
	Entry->SetStringField(TEXT("error"), TEXT("sync_graph only available in editor builds"));
#endif
}

bool FManageAssetBehaviorTreeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);

	UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath);
	if (!BT)
	{
		OutError = FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = BT;
	return true;
}

void FManageAssetBehaviorTreeCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
#if WITH_EDITOR
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Target);
	if (FNexusBehaviorTreeEditUtils::CloseOpenEditorToAvoidGraphOverwrite(BT))
	{
		OutTop->SetBoolField(TEXT("editorClosed"), true);
	}
#endif
}

void FManageAssetBehaviorTreeCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FNexusBehaviorTreeEditUtils::NotifyBehaviorTreeAssetChanged(static_cast<UBehaviorTree*>(Target));
#endif
}

void FManageAssetBehaviorTreeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_root"), &HandleBT_SetRoot);
	OutHandlers.Add(TEXT("add_node"), &HandleBT_AddNode);
	OutHandlers.Add(TEXT("move_node"), &HandleBT_MoveNode);
	OutHandlers.Add(TEXT("remove_node"), &HandleBT_RemoveNode);
	OutHandlers.Add(TEXT("replace_node"), &HandleBT_ReplaceNode);
	OutHandlers.Add(TEXT("add_decorator"), &HandleBT_AddDecorator);
	OutHandlers.Add(TEXT("remove_decorator"), &HandleBT_RemoveDecorator);
	OutHandlers.Add(TEXT("add_service"), &HandleBT_AddService);
	OutHandlers.Add(TEXT("remove_service"), &HandleBT_RemoveService);
	OutHandlers.Add(TEXT("set_blackboard"), &HandleBT_SetBlackboard);
	OutHandlers.Add(TEXT("set_property"), &HandleBT_SetProperty);
	OutHandlers.Add(TEXT("sync_graph"), &HandleBT_SyncGraph);
}

REGISTER_MCP_CAPABILITY(FManageAssetBehaviorTreeCapability)
