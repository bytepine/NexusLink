// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTService.h"
#include "NexusMcpTool.h"

// ─── 节点路径辅助 ──────────────────────────────────────────────────────────────

/** 将节点在其父 Children[] 中的下标序列拼成 "0.1.2" 路径字符串 */
static FString BuildNodePath(const TArray<int32>& Indices)
{
	FString Result;
	for (int32 i = 0; i < Indices.Num(); ++i)
	{
		if (i > 0) Result += TEXT(".");
		Result += FString::FromInt(Indices[i]);
	}
	return Result;
}

/**
 * 按点分隔的 childIndex 序列从 BT root 向下定位节点。
 * 空路径 → root 节点。路径解析失败返回 nullptr。
 */
static UBTNode* FindNodeByPath(UBehaviorTree* BT, const FString& Path)
{
	if (!BT || !BT->RootNode) return nullptr;
	if (Path.IsEmpty()) return BT->RootNode;

	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("."), true);

	UBTNode* Cur = BT->RootNode;
	for (const FString& Part : Parts)
	{
		UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Cur);
		if (!Composite) return nullptr;

		const int32 Idx = FCString::Atoi(*Part);
		if (!Composite->Children.IsValidIndex(Idx)) return nullptr;

		const FBTCompositeChild& Child = Composite->Children[Idx];
		Cur = Child.ChildComposite
			? static_cast<UBTNode*>(Child.ChildComposite)
			: static_cast<UBTNode*>(Child.ChildTask);
		if (!Cur) return nullptr;
	}
	return Cur;
}

/** 定位 parentPath 对应的 composite 节点；空路径 = root */
static UBTCompositeNode* FindCompositeByPath(UBehaviorTree* BT, const FString& Path)
{
	return Cast<UBTCompositeNode>(FindNodeByPath(BT, Path));
}

/**
 * 查找 target 节点的父 composite 及其 childIndex。
 * targetPath 不能为空（root 无父）。
 */
static bool FindParentAndIndex(UBehaviorTree* BT, const FString& TargetPath,
                               UBTCompositeNode*& OutParent, int32& OutIndex)
{
	if (TargetPath.IsEmpty()) return false;

	// 父路径 = 去掉最后一段
	int32 LastDot = INDEX_NONE;
	TargetPath.FindLastChar(TEXT('.'), LastDot);
	const FString ParentPath = (LastDot == INDEX_NONE) ? TEXT("") : TargetPath.Left(LastDot);
	const FString LastPart   = (LastDot == INDEX_NONE) ? TargetPath : TargetPath.Mid(LastDot + 1);

	UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
	if (!Parent) return false;

	const int32 Idx = FCString::Atoi(*LastPart);
	if (!Parent->Children.IsValidIndex(Idx)) return false;

	OutParent = Parent;
	OutIndex  = Idx;
	return true;
}

#if WITH_EDITOR
static void NotifyBehaviorTreeAssetChanged(UBehaviorTree* BT)
{
	if (!BT) return;
	BT->Modify();
	BT->PostEditChange();
}
#endif

// ─── Execute ─────────────────────────────────────────────────────────────────

void FManageAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_behavior_tree");
	Out.Description = TEXT("批量编辑 BT 节点/装饰器/服务。运行时树与编辑器图同步刷新。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("行为树资产路径")))
		.Prop(TEXT("action"),      FNexusSchema::Enum(TEXT("操作类型"),
			{ TEXT("set_root"), TEXT("add_node"), TEXT("remove_node"), TEXT("move_node"),
			  TEXT("add_decorator"), TEXT("remove_decorator"),
			  TEXT("add_service"),   TEXT("remove_service"),
			  TEXT("set_blackboard"), TEXT("set_property") }))
		.Prop(TEXT("nodeClass"),   FNexusSchema::Str(TEXT("节点类名（set_root/add_node/add_decorator/add_service）")))
		.Prop(TEXT("nodeName"),    FNexusSchema::Str(TEXT("显示名覆盖（可选）")))
		.Prop(TEXT("parentPath"),  FNexusSchema::Str(TEXT("从根起的点分子节点索引，如 '' 或 '0.1'")))
		.Prop(TEXT("childIndex"),  FNexusSchema::Int(TEXT("子槽索引（add_node/move_node/装饰器/服务）"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("targetPath"),  FNexusSchema::Str(TEXT("目标节点点分路径（remove_node/move_node/set_property）")))
		.Prop(TEXT("targetIndex"), FNexusSchema::Int(TEXT("decorators[]/services[] 中要删改的索引"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("blackboardPath"), FNexusSchema::Str(TEXT("BlackboardData 资产路径（set_blackboard）")))
		.Prop(TEXT("targetType"),  FNexusSchema::Enum(TEXT("set_property 的目标类型"),
			{ TEXT("node"), TEXT("decorator"), TEXT("service") }))
		.Prop(TEXT("propertyName"),  FNexusSchema::Str(TEXT("要设置的 UPROPERTY 名（set_property）")))
		.Prop(TEXT("propertyValue"), FNexusSchema::Str(TEXT("文本值，ImportText 格式（set_property）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("bt"), TEXT("node"), TEXT("decorator"), TEXT("service"), TEXT("blackboard")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_behavior_tree"), TEXT("manage_asset_blackboard"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删节点/装饰器/服务、设属性");
}

FCapabilityResult FManageAssetBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		FString Action;
		if (!Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}
		Action.ToLowerInline();

		UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath);
		if (!BT)
		{
			OutError = FString::Printf(TEXT("BehaviorTree 未找到: %s"), *AssetPath);
			return;
		}

		Entry->SetStringField(TEXT("action"), Action);

		// ── set_root ───────────────────────────────────────────────────────────────
		if (Action == TEXT("set_root"))
		{
			FString NodeClass;
			if (!Arguments->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_root 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class || !Class->IsChildOf(UBTCompositeNode::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 未找到或不是 BTCompositeNode 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBTCompositeNode* NewRoot = NewObject<UBTCompositeNode>(BT, Class);
			FString NodeName;
			if (Arguments->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewRoot->NodeName = NodeName;
			}
			BT->RootNode = NewRoot;
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif
			Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
		}
		// ── add_node ───────────────────────────────────────────────────────────────
		else if (Action == TEXT("add_node"))
		{
			FString NodeClass;
			if (!Arguments->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_node 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("nodeClass '%s' 未找到"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
			const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
			if (!bIsComposite && !bIsTask)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 须为 BTCompositeNode 或 BTTaskNode 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			FString ParentPath;
			Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			FBTCompositeChild NewChild;
			if (bIsComposite)
			{
				UBTCompositeNode* NewComp = NewObject<UBTCompositeNode>(BT, Class);
				FString NodeName;
				if (Arguments->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
				{
					NewComp->NodeName = NodeName;
				}
				NewChild.ChildComposite = NewComp;
			}
			else
			{
				UBTTaskNode* NewTask = NewObject<UBTTaskNode>(BT, Class);
				FString NodeName;
				if (Arguments->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
				{
					NewTask->NodeName = NodeName;
				}
				NewChild.ChildTask = NewTask;
			}

			const int32 InsertIdx = [&]() -> int32
			{
				if (Arguments->HasField(TEXT("childIndex")))
				{
					const int32 Idx = static_cast<int32>(Arguments->GetNumberField(TEXT("childIndex")));
					return FMath::Clamp(Idx, 0, Parent->Children.Num());
				}
				return Parent->Children.Num();
			}();
			Parent->Children.Insert(NewChild, InsertIdx);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif

			const FString AddedPath = ParentPath.IsEmpty()
				? FString::FromInt(InsertIdx)
				: ParentPath + TEXT(".") + FString::FromInt(InsertIdx);
			Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
			Entry->SetStringField(TEXT("addedPath"), AddedPath);
			Entry->SetNumberField(TEXT("childIndex"), static_cast<double>(InsertIdx));
		}
		// ── move_node ──────────────────────────────────────────────────────────────
		else if (Action == TEXT("move_node"))
		{
			FString TargetPath;
			if (!Arguments->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("move_node 需要 targetPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBTCompositeNode* SrcParent = nullptr;
			int32 SrcIdx = INDEX_NONE;
			if (!FindParentAndIndex(BT, TargetPath, SrcParent, SrcIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 无效"), *TargetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			FString NewParentPath;
			Arguments->TryGetStringField(TEXT("parentPath"), NewParentPath);
			if (NewParentPath.StartsWith(TargetPath + TEXT(".")) || NewParentPath == TargetPath)
			{
				Entry->SetStringField(TEXT("error"), TEXT("不能将节点移动到其自身或子树下"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBTCompositeNode* DstParent = FindCompositeByPath(BT, NewParentPath);
			if (!DstParent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *NewParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			const FBTCompositeChild MovedChild = SrcParent->Children[SrcIdx];
			SrcParent->Children.RemoveAt(SrcIdx);

			const int32 InsertIdx = [&]() -> int32
			{
				if (Arguments->HasField(TEXT("childIndex")))
				{
					const int32 Idx = static_cast<int32>(Arguments->GetNumberField(TEXT("childIndex")));
					return FMath::Clamp(Idx, 0, DstParent->Children.Num());
				}
				return DstParent->Children.Num();
			}();
			DstParent->Children.Insert(MovedChild, InsertIdx);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif

			const FString NewPath = NewParentPath.IsEmpty()
				? FString::FromInt(InsertIdx)
				: NewParentPath + TEXT(".") + FString::FromInt(InsertIdx);
			Entry->SetStringField(TEXT("movedPath"), NewPath);
			Entry->SetNumberField(TEXT("childIndex"), static_cast<double>(InsertIdx));
		}
		// ── remove_node ────────────────────────────────────────────────────────────
		else if (Action == TEXT("remove_node"))
		{
			FString TargetPath;
			if (!Arguments->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_node 需要 targetPath；用 set_root 替换根节点"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBTCompositeNode* Parent = nullptr;
			int32 ChildIdx           = INDEX_NONE;
			if (!FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 无效"), *TargetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			Parent->Children.RemoveAt(ChildIdx);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif
			Entry->SetStringField(TEXT("removedPath"), TargetPath);
		}
		// ── add_decorator ──────────────────────────────────────────────────────────
		else if (Action == TEXT("add_decorator"))
		{
			FString NodeClass;
			if (!Arguments->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_decorator 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class || !Class->IsChildOf(UBTDecorator::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 未找到或不是 BTDecorator 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			FString ParentPath;
			Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			int32 ChildIdx = 0;
			if (Arguments->HasField(TEXT("childIndex")))
			{
				ChildIdx = (int32)Arguments->GetNumberField(TEXT("childIndex"));
			}
			if (!Parent->Children.IsValidIndex(ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("childIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBTDecorator* Dec = NewObject<UBTDecorator>(BT, Class);
			FString NodeName;
			if (Arguments->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				Dec->NodeName = NodeName;
			}
			const int32 AddedIdx = Parent->Children[ChildIdx].Decorators.Add(Dec);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif

			Entry->SetStringField(TEXT("nodeClass"),   Class->GetName());
			Entry->SetStringField(TEXT("parentPath"),  ParentPath);
			Entry->SetNumberField(TEXT("childIndex"),  ChildIdx);
			Entry->SetNumberField(TEXT("addedIndex"),  AddedIdx);
		}
		// ── remove_decorator ───────────────────────────────────────────────────────
		else if (Action == TEXT("remove_decorator"))
		{
			FString ParentPath;
			Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			int32 ChildIdx = 0;
			if (Arguments->HasField(TEXT("childIndex"))) ChildIdx = (int32)Arguments->GetNumberField(TEXT("childIndex"));
			if (!Parent->Children.IsValidIndex(ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("childIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

		int32 TargetIdx = 0;
		if (Arguments->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Arguments->GetNumberField(TEXT("targetIndex"));
		auto& Decs = Parent->Children[ChildIdx].Decorators;
		if (!Decs.IsValidIndex(TargetIdx))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("targetIndex %d out of range [0, %d)"), TargetIdx, Decs.Num()));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Decs.RemoveAt(TargetIdx);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif
			Entry->SetStringField(TEXT("parentPath"), ParentPath);
			Entry->SetNumberField(TEXT("childIndex"), ChildIdx);
			Entry->SetNumberField(TEXT("removedIndex"), TargetIdx);
		}
		// ── add_service ────────────────────────────────────────────────────────────
		else if (Action == TEXT("add_service"))
		{
			FString NodeClass;
			if (!Arguments->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_service 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class || !Class->IsChildOf(UBTService::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 未找到或不是 BTService 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			FString ParentPath;
			Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBTService* Svc = NewObject<UBTService>(BT, Class);
			FString NodeName;
			if (Arguments->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				Svc->NodeName = NodeName;
			}
			const int32 AddedIdx = Parent->Services.Add(Svc);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif

			Entry->SetStringField(TEXT("nodeClass"),  Class->GetName());
			Entry->SetStringField(TEXT("parentPath"), ParentPath);
			Entry->SetNumberField(TEXT("addedIndex"), AddedIdx);
		}
		// ── remove_service ─────────────────────────────────────────────────────────
		else if (Action == TEXT("remove_service"))
		{
			FString ParentPath;
			Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			int32 TargetIdx = 0;
			if (Arguments->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Arguments->GetNumberField(TEXT("targetIndex"));
			if (!Parent->Services.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("targetIndex %d out of range [0, %d)"), TargetIdx, Parent->Services.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			Parent->Services.RemoveAt(TargetIdx);
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif
			Entry->SetStringField(TEXT("parentPath"),   ParentPath);
			Entry->SetNumberField(TEXT("removedIndex"), TargetIdx);
		}
		// ── set_blackboard ─────────────────────────────────────────────────────────
		else if (Action == TEXT("set_blackboard"))
		{
			FString BBPath;
			if (!Arguments->TryGetStringField(TEXT("blackboardPath"), BBPath) || BBPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_blackboard 需要 blackboardPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			UBlackboardData* BBAsset = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(BBPath);
			if (!BBAsset)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("BlackboardData 未找到: %s"), *BBPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			BT->BlackboardAsset = BBAsset;
			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif
			Entry->SetStringField(TEXT("blackboardPath"), BBAsset->GetPathName());
		}
		// ── set_property ───────────────────────────────────────────────────────────
		else if (Action == TEXT("set_property"))
		{
			FString PropertyName;
			if (!Arguments->TryGetStringField(TEXT("propertyName"), PropertyName) || PropertyName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			FString PropertyValue;
			if (!Arguments->TryGetStringField(TEXT("propertyValue"), PropertyValue))
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyValue"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 确定目标类型：node / decorator / service
			FString TargetType = TEXT("node");
			Arguments->TryGetStringField(TEXT("targetType"), TargetType);

			UBTNode* TargetNode = nullptr;

			if (TargetType == TEXT("node"))
			{
				FString TargetPath;
				Arguments->TryGetStringField(TEXT("targetPath"), TargetPath);
				TargetNode = FindNodeByPath(BT, TargetPath);
				if (!TargetNode)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 处未找到节点"), *TargetPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
			}
			else if (TargetType == TEXT("decorator"))
			{
				FString ParentPath;
				Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
				UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
				if (!Parent)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("parentPath '%s' 未找到"), *ParentPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}

				int32 ChildIdx = 0;
				if (Arguments->HasField(TEXT("childIndex"))) ChildIdx = (int32)Arguments->GetNumberField(TEXT("childIndex"));
				if (!Parent->Children.IsValidIndex(ChildIdx))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("childIndex %d out of range"), ChildIdx));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}

			int32 TargetIdx = 0;
			if (Arguments->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Arguments->GetNumberField(TEXT("targetIndex"));
			auto& Decs = Parent->Children[ChildIdx].Decorators;
				if (!Decs.IsValidIndex(TargetIdx))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetIndex %d out of range"), TargetIdx));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				TargetNode = Decs[TargetIdx];
			}
			else if (TargetType == TEXT("service"))
			{
				FString ParentPath;
				Arguments->TryGetStringField(TEXT("parentPath"), ParentPath);
				UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
				if (!Parent)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("parentPath '%s' 未找到"), *ParentPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}

				int32 TargetIdx = 0;
				if (Arguments->HasField(TEXT("targetIndex"))) TargetIdx = (int32)Arguments->GetNumberField(TEXT("targetIndex"));
				if (!Parent->Services.IsValidIndex(TargetIdx))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetIndex %d out of range"), TargetIdx));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				TargetNode = Parent->Services[TargetIdx];
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown targetType: '%s'"), *TargetType));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 通过反射设置属性
			FProperty* Prop = TargetNode->GetClass()->FindPropertyByName(*PropertyName);
			if (!Prop)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("在 %s 上未找到属性 '%s'"), *PropertyName, *TargetNode->GetClass()->GetName()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
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
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("设置 '%s' = '%s' 失败（ImportText 失败）"), *PropertyName, *PropertyValue));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			BT->MarkPackageDirty();
#if WITH_EDITOR
			NotifyBehaviorTreeAssetChanged(BT);
#endif
			Entry->SetStringField(TEXT("targetType"),    TargetType);
			Entry->SetStringField(TEXT("propertyName"),  PropertyName);
			Entry->SetStringField(TEXT("propertyValue"), PropertyValue);
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetBehaviorTreeCapability)
