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

/** 把 ApplyInitialProperties 收集到的失败项写进本条 Entry 的 PropertyErrors[] */
static void ReportPropertyErrors(const TSharedPtr<FJsonObject>& Entry, const TArray<FString>& Errors)
{
	if (!Entry.IsValid() || Errors.Num() == 0) return;
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& E : Errors) Arr.Add(MakeShared<FJsonValueString>(E));
	Entry->SetArrayField(TEXT("PropertyErrors"), Arr);
}

// ─── Execute ─────────────────────────────────────────────────────────────────

void FNexusManageAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetBehaviorTree");
	Out.SearchAssetTypes = {TEXT("BehaviorTree")};
	// 详细语义（图同步边界、ReplaceNode vs remove+add）见 docs/tool-reference.md，此处保持 ≤100 字符
	Out.Description = TEXT("Batch edit BT nodes/decorators/services. ReplaceNode swaps type; SyncGraph fixes graph drift.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),      FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("SetRoot"), TEXT("AddNode"), TEXT("RemoveNode"), TEXT("ReplaceNode"), TEXT("MoveNode"),
			  TEXT("AddDecorator"), TEXT("RemoveDecorator"),
			  TEXT("AddService"),   TEXT("RemoveService"),
			  TEXT("SetBlackboard"), TEXT("SetProperty"), TEXT("SyncGraph") }))
		.Prop(TEXT("NodeClass"),   FNexusSchema::Str(TEXT("Node class (SetRoot/AddNode/ReplaceNode/AddDecorator/AddService)")))
		.Prop(TEXT("NodeName"),    FNexusSchema::Str(TEXT("Display name override (optional)")))
		.Prop(TEXT("ParentPath"),  FNexusSchema::Str(TEXT("Child index path from root, e.g. '' or '0.1'")))
		.Prop(TEXT("ChildIndex"),  FNexusSchema::Int(TEXT("Child slot index (AddNode/MoveNode/decorator/service)"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("TargetPath"),  FNexusSchema::Str(TEXT("Target node dot path (RemoveNode/ReplaceNode/MoveNode/SetProperty)")))
		.Prop(TEXT("TargetIndex"), FNexusSchema::Int(TEXT("Index in decorators[]/services[] to edit/remove"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("BlackboardPath"), FNexusSchema::Str(TEXT("BlackboardData asset path (SetBlackboard)")))
		.Prop(TEXT("TargetType"),  FNexusSchema::Enum(TEXT("Target type for SetProperty"),
			{ TEXT("Node"), TEXT("Decorator"), TEXT("Service") }))
		.Prop(TEXT("PropertyName"),  FNexusSchema::Str(TEXT("UPROPERTY name to set (SetProperty)")))
		.Prop(TEXT("PropertyValue"), FNexusSchema::Str(TEXT("Text value, ImportText format (SetProperty)")))
		.Prop(TEXT("properties"),  FNexusSchema::ArrOfObj(TEXT("Initial props for AddNode/ReplaceNode [{name,value}]")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("BehaviorTree asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("bt"), TEXT("Node"), TEXT("Decorator"), TEXT("Service"), TEXT("Blackboard"),
		TEXT("replace"), TEXT("graph"), TEXT("sync")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetBehaviorTree"), TEXT("ManageAssetBlackboard"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/replace/move nodes, decorators, services, set props; SyncGraph when graph/tree drift");
}

static void HandleBT_SetRoot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("NodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("SetRoot requires NodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class || !Class->IsChildOf(UBTCompositeNode::StaticClass()))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("NodeClass '%s' not found or not BTCompositeNode subclass"), *NodeClass));
			return;
		}

		UBTCompositeNode* NewRoot = NewObject<UBTCompositeNode>(BT, Class);
		FString NodeName;
		if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
		{
			NewRoot->NodeName = NodeName;
		}
		BT->RootNode = NewRoot;
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("NodeClass"), Class->GetName());
}
static void HandleBT_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("NodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("AddNode requires NodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("NodeClass '%s' not found"), *NodeClass));
			return;
		}
		const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
		const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
		if (!bIsComposite && !bIsTask)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("NodeClass '%s' must be BTCompositeNode or BTTaskNode subclass"), *NodeClass));
			return;
		}

		FString ParentPath;
		Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ParentPath '%s' is not a composite node or does not exist"), *ParentPath));
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
			if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
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
			if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewTask->NodeName = NodeName;
			}
			NewChild.ChildTask = NewTask;
			CreatedNode = NewTask;
		}

		// 支持在创建节点时直接设置初始属性（避免后续 SetProperty 因类卸载而崩溃）
		TArray<FString> PropErrors;
		FNexusBehaviorTreeEditUtils::ApplyInitialProperties(CreatedNode, Op, PropErrors);
		ReportPropertyErrors(Entry, PropErrors);

		const int32 InsertIdx = [&]() -> int32
		{
			if (Op->HasField(TEXT("ChildIndex")))
			{
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("ChildIndex")));
				return FMath::Clamp(Idx, 0, Parent->Children.Num());
			}
			return Parent->Children.Num();
		}();
		Parent->Children.Insert(NewChild, InsertIdx);
		BT->MarkPackageDirty();

		const FString AddedPath = ParentPath.IsEmpty()
			? FString::FromInt(InsertIdx)
			: ParentPath + TEXT(".") + FString::FromInt(InsertIdx);
		Entry->SetStringField(TEXT("NodeClass"), Class->GetName());
		Entry->SetStringField(TEXT("AddedPath"), AddedPath);
		Entry->SetNumberField(TEXT("ChildIndex"), static_cast<double>(InsertIdx));
}
static void HandleBT_MoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString TargetPath;
		if (!Op->TryGetStringField(TEXT("TargetPath"), TargetPath) || TargetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("MoveNode requires TargetPath"));
			return;
		}

		UBTCompositeNode* SrcParent = nullptr;
		int32 SrcIdx = INDEX_NONE;
		if (!FNexusBehaviorTreeEditUtils::FindParentAndIndex(BT, TargetPath, SrcParent, SrcIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid TargetPath '%s'"), *TargetPath));
			return;
		}

		FString NewParentPath;
		Op->TryGetStringField(TEXT("ParentPath"), NewParentPath);
		if (NewParentPath.StartsWith(TargetPath + TEXT(".")) || NewParentPath == TargetPath)
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Cannot move node into itself or its subtree"));
			return;
		}

		UBTCompositeNode* DstParent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, NewParentPath);
		if (!DstParent)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ParentPath '%s' is not a composite node or does not exist"), *NewParentPath));
			return;
		}

		const FBTCompositeChild MovedChild = SrcParent->Children[SrcIdx];
		SrcParent->Children.RemoveAt(SrcIdx);

		const int32 InsertIdx = [&]() -> int32
		{
			if (Op->HasField(TEXT("ChildIndex")))
			{
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("ChildIndex")));
				return FMath::Clamp(Idx, 0, DstParent->Children.Num());
			}
			return DstParent->Children.Num();
		}();
		DstParent->Children.Insert(MovedChild, InsertIdx);
		BT->MarkPackageDirty();

		const FString NewPath = NewParentPath.IsEmpty()
			? FString::FromInt(InsertIdx)
			: NewParentPath + TEXT(".") + FString::FromInt(InsertIdx);
		Entry->SetStringField(TEXT("MovedPath"), NewPath);
		Entry->SetNumberField(TEXT("ChildIndex"), static_cast<double>(InsertIdx));
}
static void HandleBT_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString TargetPath;
		if (!Op->TryGetStringField(TEXT("TargetPath"), TargetPath) || TargetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("RemoveNode requires TargetPath; use SetRoot to replace root"));
			return;
		}

		UBTCompositeNode* Parent = nullptr;
		int32 ChildIdx           = INDEX_NONE;
		if (!FNexusBehaviorTreeEditUtils::FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid TargetPath '%s'"), *TargetPath));
			return;
		}

		Parent->Children.RemoveAt(ChildIdx);
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("RemovedPath"), TargetPath);
}
static void HandleBT_ReplaceNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
	// 原子操作：就地替换某个已有子节点的类型（decorators/services 保留在同槽位）。
		FString TargetPath;
		if (!Op->TryGetStringField(TEXT("TargetPath"), TargetPath) || TargetPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("ReplaceNode requires TargetPath; use SetRoot to replace root"));
			return;
		}

		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("NodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("ReplaceNode requires NodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("NodeClass '%s' not found"), *NodeClass));
			return;
		}
		const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
		const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
		if (!bIsComposite && !bIsTask)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("NodeClass '%s' must be BTCompositeNode or BTTaskNode subclass"), *NodeClass));
			return;
		}

		UBTCompositeNode* Parent = nullptr;
		int32 ChildIdx           = INDEX_NONE;
		if (!FNexusBehaviorTreeEditUtils::FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid TargetPath '%s'"), *TargetPath));
			return;
		}

		FBTCompositeChild& Slot = Parent->Children[ChildIdx];
		const bool bOldIsComposite = Slot.ChildComposite != nullptr;
		if (bOldIsComposite != bIsComposite)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
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
			if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
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
			if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
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
		Entry->SetBoolField(TEXT("bGraphSynced"), bGraphSynced);
#endif

		Entry->SetStringField(TEXT("ReplacedPath"),   TargetPath);
		Entry->SetStringField(TEXT("OldNodeClass"),   OldNodeClassName);
		Entry->SetStringField(TEXT("NodeClass"),      Class->GetName());
		if (MovedChildren > 0) Entry->SetNumberField(TEXT("MovedChildren"), MovedChildren);
		if (MovedServices > 0) Entry->SetNumberField(TEXT("MovedServices"), MovedServices);
}
static void HandleBT_AddDecorator(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("NodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("AddDecorator requires NodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class || !Class->IsChildOf(UBTDecorator::StaticClass()))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("NodeClass '%s' not found or not BTDecorator subclass"), *NodeClass));
			return;
		}

		FString ParentPath;
		Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ParentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		int32 ChildIdx = 0;
		if (Op->HasField(TEXT("ChildIndex")))
		{
			ChildIdx = (int32)Op->GetNumberField(TEXT("ChildIndex"));
		}
		if (!Parent->Children.IsValidIndex(ChildIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ChildIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
			return;
		}

		UBTDecorator* Dec = NewObject<UBTDecorator>(BT, Class);
		FString NodeName;
		if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
		{
			Dec->NodeName = NodeName;
		}
		const int32 AddedIdx = Parent->Children[ChildIdx].Decorators.Add(Dec);
		BT->MarkPackageDirty();

		Entry->SetStringField(TEXT("NodeClass"),   Class->GetName());
		Entry->SetStringField(TEXT("ParentPath"),  ParentPath);
		Entry->SetNumberField(TEXT("ChildIndex"),  ChildIdx);
		Entry->SetNumberField(TEXT("AddedIndex"),  AddedIdx);
}
static void HandleBT_RemoveDecorator(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString ParentPath;
		Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ParentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		int32 ChildIdx = 0;
		if (Op->HasField(TEXT("ChildIndex"))) ChildIdx = (int32)Op->GetNumberField(TEXT("ChildIndex"));
		if (!Parent->Children.IsValidIndex(ChildIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ChildIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
			return;
		}

		int32 TargetIdx = 0;
		if (Op->HasField(TEXT("TargetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("TargetIndex"));
		auto& Decs = Parent->Children[ChildIdx].Decorators;
		if (!Decs.IsValidIndex(TargetIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("TargetIndex %d out of range [0, %d)"), TargetIdx, Decs.Num()));
			return;
		}

		Decs.RemoveAt(TargetIdx);
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("ParentPath"), ParentPath);
		Entry->SetNumberField(TEXT("ChildIndex"), ChildIdx);
		Entry->SetNumberField(TEXT("RemovedIndex"), TargetIdx);
}
static void HandleBT_AddService(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString NodeClass;
		if (!Op->TryGetStringField(TEXT("NodeClass"), NodeClass) || NodeClass.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("AddService requires NodeClass"));
			return;
		}

		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
		if (!Class || !Class->IsChildOf(UBTService::StaticClass()))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("NodeClass '%s' not found or not BTService subclass"), *NodeClass));
			return;
		}

		FString ParentPath;
		Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ParentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		UBTService* Svc = NewObject<UBTService>(BT, Class);
		FString NodeName;
		if (Op->TryGetStringField(TEXT("NodeName"), NodeName) && !NodeName.IsEmpty())
		{
			Svc->NodeName = NodeName;
		}
		const int32 AddedIdx = Parent->Services.Add(Svc);
		BT->MarkPackageDirty();

		Entry->SetStringField(TEXT("NodeClass"),  Class->GetName());
		Entry->SetStringField(TEXT("ParentPath"), ParentPath);
		Entry->SetNumberField(TEXT("AddedIndex"), AddedIdx);
}
static void HandleBT_RemoveService(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString ParentPath;
		Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
		UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
		if (!Parent)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("ParentPath '%s' is not a composite node or does not exist"), *ParentPath));
			return;
		}

		int32 TargetIdx = 0;
		if (Op->HasField(TEXT("TargetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("TargetIndex"));
		if (!Parent->Services.IsValidIndex(TargetIdx))
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("TargetIndex %d out of range [0, %d)"), TargetIdx, Parent->Services.Num()));
			return;
		}

		Parent->Services.RemoveAt(TargetIdx);
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("ParentPath"),   ParentPath);
		Entry->SetNumberField(TEXT("RemovedIndex"), TargetIdx);
}
static void HandleBT_SetBlackboard(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString BBPath;
		if (!Op->TryGetStringField(TEXT("BlackboardPath"), BBPath) || BBPath.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("SetBlackboard requires BlackboardPath"));
			return;
		}

		UBlackboardData* BBAsset = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(BBPath);
		if (!BBAsset)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("BlackboardData not found: %s"), *BBPath));
			return;
		}

		BT->BlackboardAsset = BBAsset;
		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("BlackboardPath"), BBAsset->GetPathName());
}
static void HandleBT_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
		FString PropertyName;
		if (!Op->TryGetStringField(TEXT("PropertyName"), PropertyName) || PropertyName.IsEmpty())
		{
			Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyName"));
			return;
		}

		FString PropertyValue;
		if (!Op->TryGetStringField(TEXT("PropertyValue"), PropertyValue))
		{
			Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyValue"));
			return;
		}

		// 确定目标类型：node / decorator / service
		FString TargetType = TEXT("Node");
		Op->TryGetStringField(TEXT("TargetType"), TargetType);

		UBTNode* TargetNode = nullptr;

		if (TargetType == TEXT("Node"))
		{
			FString TargetPath;
			Op->TryGetStringField(TEXT("TargetPath"), TargetPath);
			TargetNode = FNexusBehaviorTreeEditUtils::FindNodeByPath(BT, TargetPath);
			if (!TargetNode)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Node not found at TargetPath '%s'"), *TargetPath));
				return;
			}
		}
		else if (TargetType == TEXT("Decorator"))
		{
			FString ParentPath;
			Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
			UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ParentPath '%s' not found"), *ParentPath));
				return;
			}

			int32 ChildIdx = 0;
			if (Op->HasField(TEXT("ChildIndex"))) ChildIdx = (int32)Op->GetNumberField(TEXT("ChildIndex"));
			if (!Parent->Children.IsValidIndex(ChildIdx))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ChildIndex %d out of range"), ChildIdx));
				return;
			}

			int32 TargetIdx = 0;
			if (Op->HasField(TEXT("TargetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("TargetIndex"));
			auto& Decs = Parent->Children[ChildIdx].Decorators;
			if (!Decs.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("TargetIndex %d out of range"), TargetIdx));
				return;
			}
			TargetNode = Decs[TargetIdx];
		}
		else if (TargetType == TEXT("Service"))
		{
			FString ParentPath;
			Op->TryGetStringField(TEXT("ParentPath"), ParentPath);
			UBTCompositeNode* Parent = FNexusBehaviorTreeEditUtils::FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ParentPath '%s' not found"), *ParentPath));
				return;
			}

			int32 TargetIdx = 0;
			if (Op->HasField(TEXT("TargetIndex"))) TargetIdx = (int32)Op->GetNumberField(TEXT("TargetIndex"));
			if (!Parent->Services.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("TargetIndex %d out of range"), TargetIdx));
				return;
			}
			TargetNode = Parent->Services[TargetIdx];
		}
		else
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown TargetType: '%s'"), *TargetType));
			return;
		}

		// 通过反射设置属性
		UClass* TargetNodeClass = TargetNode->GetClass();
		if (!TargetNodeClass)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Node '%s' GetClass() returned null (class may be unloaded)"), *TargetNode->GetName()));
			return;
		}
		FProperty* Prop = TargetNodeClass->FindPropertyByName(*PropertyName);
		if (!Prop)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Property '%s' not found on %s"), *TargetNodeClass->GetName(), *PropertyName));
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
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to set '%s' = '%s' (ImportText failed)"), *PropertyName, *PropertyValue));
			return;
		}

		BT->MarkPackageDirty();
		Entry->SetStringField(TEXT("TargetType"),    TargetType);
		Entry->SetStringField(TEXT("PropertyName"),  PropertyName);
		Entry->SetStringField(TEXT("PropertyValue"), PropertyValue);
}
static void HandleBT_SyncGraph(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Ctx.Target);
	TSharedPtr<FJsonObject>& Entry = Ctx.Entry;
#if WITH_EDITOR
		FString Msg;
		TArray<FString> Warnings;
		const bool bOk = FNexusBehaviorTreeEditUtils::RebuildGraphFromRootNode(BT, Msg, Warnings);
		Entry->SetBoolField(TEXT("bGraphSynced"), bOk);
		Entry->SetStringField(TEXT("Message"), Msg);
		if (Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarnArr;
			for (const FString& W : Warnings) WarnArr.Add(MakeShared<FJsonValueString>(W));
			Entry->SetArrayField(TEXT("Warnings"), WarnArr);
		}
#else
	Entry->SetStringField(TEXT("Error"), TEXT("SyncGraph only available in editor builds"));
#endif
}

bool FNexusManageAssetBehaviorTreeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);

	UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath);
	if (!BT)
	{
		OutError = FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = BT;
	return true;
}

void FNexusManageAssetBehaviorTreeCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
#if WITH_EDITOR
	UBehaviorTree* BT = static_cast<UBehaviorTree*>(Target);
	if (FNexusBehaviorTreeEditUtils::CloseOpenEditorToAvoidGraphOverwrite(BT))
	{
		OutTop->SetBoolField(TEXT("bEditorClosed"), true);
	}
#endif
}

void FNexusManageAssetBehaviorTreeCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FNexusBehaviorTreeEditUtils::NotifyBehaviorTreeAssetChanged(static_cast<UBehaviorTree*>(Target));
#endif
}

void FNexusManageAssetBehaviorTreeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetRoot"), &HandleBT_SetRoot);
	OutHandlers.Add(TEXT("AddNode"), &HandleBT_AddNode);
	OutHandlers.Add(TEXT("MoveNode"), &HandleBT_MoveNode);
	OutHandlers.Add(TEXT("RemoveNode"), &HandleBT_RemoveNode);
	OutHandlers.Add(TEXT("ReplaceNode"), &HandleBT_ReplaceNode);
	OutHandlers.Add(TEXT("AddDecorator"), &HandleBT_AddDecorator);
	OutHandlers.Add(TEXT("RemoveDecorator"), &HandleBT_RemoveDecorator);
	OutHandlers.Add(TEXT("AddService"), &HandleBT_AddService);
	OutHandlers.Add(TEXT("RemoveService"), &HandleBT_RemoveService);
	OutHandlers.Add(TEXT("SetBlackboard"), &HandleBT_SetBlackboard);
	OutHandlers.Add(TEXT("SetProperty"), &HandleBT_SetProperty);
	OutHandlers.Add(TEXT("SyncGraph"), &HandleBT_SyncGraph);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetBehaviorTreeCapability)
