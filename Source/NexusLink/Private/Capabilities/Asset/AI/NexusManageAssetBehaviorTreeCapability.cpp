// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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
#if WITH_EDITOR
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"
#endif

// ─── 节点路径辅助 ──────────────────────────────────────────────────────────────

/** 解析点分路径中的一段：必须非空且全为数字，否则视为非法路径（Atoi 会把 "abc" 静默当成 0） */
static bool ParseChildIndexToken(const FString& Token, int32& OutIndex)
{
	if (Token.IsEmpty()) return false;
	for (const TCHAR C : Token)
	{
		if (!FChar::IsDigit(C)) return false;
	}
	OutIndex = FCString::Atoi(*Token);
	return true;
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

		int32 Idx = INDEX_NONE;
		if (!ParseChildIndexToken(Part, Idx)) return nullptr;
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

	int32 Idx = INDEX_NONE;
	if (!ParseChildIndexToken(LastPart, Idx)) return false;
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

/**
 * 若该资产当前在编辑器中打开，关闭其编辑器 Tab（不保存）。
 *
 * 原因：本接口直接改写 UBehaviorTree::RootNode/Children（运行时树）。虽然 replace_node
 * 现在会尽力同步可视化 EdGraph 对应节点的 NodeInstance（见 SyncEdGraphNodeInstance），
 * 但如果资产的编辑器 Tab 在写入时仍打开着，编辑器内存中缓存的旧 UI 状态不会自动感知这次
 * 外部修改；用户之后在编辑器里点击 Save/Compile 时可能用内存中尚未刷新的状态重新序列化，
 * 把刚写入的修改覆盖回旧节点。因此在执行写操作前主动关闭编辑器 Tab，确保下次重新打开时
 * 从磁盘（已同步好 Graph 的最新数据）重新加载。
 */
static bool CloseOpenEditorToAvoidGraphOverwrite(UBehaviorTree* BT)
{
	if (!BT || !GEditor) return false;
	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		if (AssetEditorSubsystem->FindEditorsForAsset(BT).Num() > 0)
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(BT);
			return true;
		}
	}
	return false;
}

/**
 * 从 SubNodes 这类对象数组里按下标取元素。
 * 必须走 FObjectPropertyBase 反射取值：UE5 起 SubNodes 声明为 TArray<TObjectPtr<UAIGraphNode>>，
 * 开启 late-resolve 时裸 reinterpret_cast 读到的是未解析句柄（带 tag 位）而非真实指针。
 */
static UObject* GetObjectArrayElement(const FArrayProperty* ArrayProp, FScriptArrayHelper& Helper, int32 Index)
{
	const FObjectPropertyBase* InnerProp = ArrayProp ? CastField<FObjectPropertyBase>(ArrayProp->Inner) : nullptr;
	if (!InnerProp || !Helper.IsValidIndex(Index)) return nullptr;
	return InnerProp->GetObjectPropertyValue(Helper.GetRawPtr(Index));
}

/**
 * 递归在图节点及其 SubNodes（AIGraphNode::SubNodes，部分引擎版本用它挂 decorator/service
 * 子节点，而不是平铺进 Graph->Nodes）里查找 NodeInstance == OldInstance 的图节点。
 *
 * 全程通过反射（FindFProperty）按属性名取值，不直接引用 UAIGraphNode/UBehaviorTreeGraphNode
 * 类型，从而不需要给本模块额外增加 AIGraph/BehaviorTreeEditor 编辑器模块依赖，
 * 也规避不同引擎版本下这些类结构可能存在的差异。
 */
static UEdGraphNode* FindGraphNodeForInstanceRec(UObject* GraphNodeObj, UObject* OldInstance)
{
	UEdGraphNode* GraphNode = Cast<UEdGraphNode>(GraphNodeObj);
	if (!GraphNode) return nullptr;

	if (FObjectProperty* InstanceProp = FindFProperty<FObjectProperty>(GraphNode->GetClass(), TEXT("NodeInstance")))
	{
		if (InstanceProp->GetObjectPropertyValue_InContainer(GraphNode) == OldInstance)
		{
			return GraphNode;
		}
	}

	if (FArrayProperty* SubNodesProp = FindFProperty<FArrayProperty>(GraphNode->GetClass(), TEXT("SubNodes")))
	{
		FScriptArrayHelper Helper(SubNodesProp, SubNodesProp->ContainerPtrToValuePtr<void>(GraphNode));
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			UObject* SubObj = GetObjectArrayElement(SubNodesProp, Helper, i);
			if (UEdGraphNode* Found = FindGraphNodeForInstanceRec(SubObj, OldInstance))
			{
				return Found;
			}
		}
	}
	return nullptr;
}

/**
 * 将 replace_node 刚创建的新运行时节点同步进可视化 EdGraph：
 * 在 BT->BTGraph 里找到 NodeInstance == OldNode 的图节点，把它的 NodeInstance 换成 NewNode。
 * 这样：
 *   1) 编辑器再次打开该 BT 时，图节点标题/属性面板显示的就是新节点（不再是旧类型）；
 *   2) 后续在编辑器里保存也不会把 RootNode 冲正回旧节点，因为图上引用的已经是新实例。
 * 若该 BT 从未在编辑器打开过（没有 BTGraph）或图上找不到对应节点，返回 false——
 * 这不算错误，只是跳过图同步（下次在编辑器打开时会按当前 RootNode 生成新图）。
 */
static bool SyncEdGraphNodeInstance(UBehaviorTree* BT, UBTNode* OldNode, UBTNode* NewNode)
{
	if (!BT || !OldNode || !NewNode) return false;

	FObjectProperty* GraphProp = FindFProperty<FObjectProperty>(BT->GetClass(), TEXT("BTGraph"));
	UObject* GraphObj = GraphProp ? GraphProp->GetObjectPropertyValue_InContainer(BT) : nullptr;
	UEdGraph* Graph = Cast<UEdGraph>(GraphObj);
	if (!Graph) return false;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UEdGraphNode* Found = FindGraphNodeForInstanceRec(Node, OldNode);
		if (!Found) continue;

		FObjectProperty* InstanceProp = FindFProperty<FObjectProperty>(Found->GetClass(), TEXT("NodeInstance"));
		if (!InstanceProp) continue;

		Graph->Modify();
		Found->Modify();
		InstanceProp->SetObjectPropertyValue_InContainer(Found, NewNode);
		Graph->MarkPackageDirty();
		return true;
	}
	return false;
}

// ─── sync_graph：按结构位置整体重建 Graph↔RootNode 对应关系 ───────────────────
//
// SyncEdGraphNodeInstance（用于 replace_node）靠"本次调用中被替换掉的旧节点对象指针"
// 去匹配 Graph 节点，只对"未来的替换"有效：如果 RootNode 在更早的调用/会话中就已经被
// 改写过（旧对象指针已经丢失，不再挂在 RootNode 树上），就永远匹配不上了。
//
// sync_graph 不依赖任何"旧指针"，而是同时按同一套深度优先顺序遍历 RootNode 树和
// Graph 树（通过输出 Pin 的连线 + SubNodes 承载 decorator/service），
// 逐位置强制把 Graph 节点的 NodeInstance 覆盖成 RootNode 树里当前的真实节点对象。
// 因此无论 RootNode 是何时被改写的，只要两棵树的结构（节点数量/子节点顺序）一致，
// 都能重新对齐。

static UObject* GetGraphNodeInstance(UEdGraphNode* Node)
{
	if (!Node) return nullptr;
	FObjectProperty* Prop = FindFProperty<FObjectProperty>(Node->GetClass(), TEXT("NodeInstance"));
	return Prop ? Prop->GetObjectPropertyValue_InContainer(Node) : nullptr;
}

static void SetGraphNodeInstance(UEdGraphNode* Node, UObject* NewInstance)
{
	if (!Node) return;
	if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(Node->GetClass(), TEXT("NodeInstance")))
	{
		Node->Modify();
		Prop->SetObjectPropertyValue_InContainer(Node, NewInstance);
	}
}

static TArray<UEdGraphNode*> GetGraphSubNodes(UEdGraphNode* Node)
{
	TArray<UEdGraphNode*> Result;
	if (!Node) return Result;
	FArrayProperty* SubNodesProp = FindFProperty<FArrayProperty>(Node->GetClass(), TEXT("SubNodes"));
	if (!SubNodesProp) return Result;
	FScriptArrayHelper Helper(SubNodesProp, SubNodesProp->ContainerPtrToValuePtr<void>(Node));
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		if (UEdGraphNode* SubNode = Cast<UEdGraphNode>(GetObjectArrayElement(SubNodesProp, Helper, i)))
		{
			Result.Add(SubNode);
		}
	}
	return Result;
}

/** 判断一个 SubNode 图节点是不是"装饰器"图节点：优先看其 NodeInstance 实际类型，没有实例时退化按图节点类名猜。 */
static bool IsDecoratorGraphNode(UEdGraphNode* SubNode)
{
	if (!SubNode) return false;
	if (UObject* Inst = GetGraphNodeInstance(SubNode)) return Inst->IsA(UBTDecorator::StaticClass());
	return SubNode->GetClass()->GetName().Contains(TEXT("Decorator"));
}

/** 判断一个 SubNode 图节点是不是"服务"图节点，逻辑同上。 */
static bool IsServiceGraphNode(UEdGraphNode* SubNode)
{
	if (!SubNode) return false;
	if (UObject* Inst = GetGraphNodeInstance(SubNode)) return Inst->IsA(UBTService::StaticClass());
	return SubNode->GetClass()->GetName().Contains(TEXT("Service"));
}

/**
 * 取一个图节点所有输出 Pin 连到的下游节点，按 NodePosX 升序返回。
 * 必须按 X 排序而不能用 LinkedTo 原始顺序：引擎编译 Graph→BT 时也是
 * `Pin->LinkedTo.Sort(FCompareNodeXLocation())`（BehaviorTreeGraph.cpp），
 * 即 Children[] 的真实次序由节点在画面上的左右位置决定，与连线先后无关。
 */
static TArray<UEdGraphNode*> GetGraphOutputChildren(UEdGraphNode* Node)
{
	TArray<UEdGraphNode*> Result;
	if (!Node) return Result;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output) continue;
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && Linked->GetOwningNode())
			{
				Result.Add(Linked->GetOwningNode());
			}
		}
	}
	Result.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return A.NodePosX < B.NodePosX;
	});
	return Result;
}

/** 在 Graph->Nodes 里找类名含 "Root" 的辅助节点，取它输出连线指向的第一个节点，即真正代表 BT->RootNode 的可视化节点。 */
static UEdGraphNode* FindRootVisualGraphNode(UEdGraph* Graph)
{
	if (!Graph) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->GetClass()->GetName().Contains(TEXT("Root")))
		{
			TArray<UEdGraphNode*> Linked = GetGraphOutputChildren(Node);
			if (Linked.Num() > 0) return Linked[0];
		}
	}
	return nullptr;
}

struct FGraphSyncStats
{
	int32 MatchedCount = 0;
	TArray<FString> Warnings;
};

/** 深度优先，把 RootTreeNode 及其子树逐位置对齐到 GraphNode 及其可视化子树，强制覆盖 NodeInstance。 */
static void SyncNodePairRecursive(UBTNode* RootTreeNode, UEdGraphNode* GraphNode, FGraphSyncStats& Stats)
{
	if (!RootTreeNode || !GraphNode) return;

	SetGraphNodeInstance(GraphNode, RootTreeNode);
	Stats.MatchedCount++;

	UBTCompositeNode* AsComposite = Cast<UBTCompositeNode>(RootTreeNode);
	if (!AsComposite) return; // Task 节点没有 Services/Children，只需要同步自身，装饰器由父节点处理

	// 服务：挂在 composite 自身的 SubNodes 上
	TArray<UEdGraphNode*> ServiceGraphNodes;
	for (UEdGraphNode* Sub : GetGraphSubNodes(GraphNode))
	{
		if (IsServiceGraphNode(Sub)) ServiceGraphNodes.Add(Sub);
	}
	const TArray<UBTService*>& Svcs = AsComposite->Services;
	const int32 SvcN = FMath::Min(Svcs.Num(), ServiceGraphNodes.Num());
	for (int32 i = 0; i < SvcN; ++i) SetGraphNodeInstance(ServiceGraphNodes[i], Svcs[i]);
	if (Svcs.Num() != ServiceGraphNodes.Num())
	{
		Stats.Warnings.Add(FString::Printf(TEXT("%s: services 数量不一致 (tree=%d, graph=%d)，仅前 %d 项已同步"),
			*RootTreeNode->GetName(), Svcs.Num(), ServiceGraphNodes.Num(), SvcN));
	}

	// 子节点：按输出 Pin 连线顺序对齐 Children[] 顺序
	TArray<UEdGraphNode*> ChildGraphNodes = GetGraphOutputChildren(GraphNode);
	const int32 ChildN = FMath::Min(AsComposite->Children.Num(), ChildGraphNodes.Num());
	if (AsComposite->Children.Num() != ChildGraphNodes.Num())
	{
		Stats.Warnings.Add(FString::Printf(TEXT("%s: 子节点数量不一致 (tree=%d, graph=%d)，仅前 %d 项已同步"),
			*RootTreeNode->GetName(), AsComposite->Children.Num(), ChildGraphNodes.Num(), ChildN));
	}

	for (int32 i = 0; i < ChildN; ++i)
	{
		const FBTCompositeChild& Child = AsComposite->Children[i];
		UBTNode* ChildNode = Child.ChildComposite
			? static_cast<UBTNode*>(Child.ChildComposite)
			: static_cast<UBTNode*>(Child.ChildTask);
		UEdGraphNode* ChildGraphNode = ChildGraphNodes[i];
		if (!ChildNode || !ChildGraphNode) continue;

		SyncNodePairRecursive(ChildNode, ChildGraphNode, Stats);

		// 该子节点自身挂的装饰器，挂在 ChildGraphNode 的 SubNodes 上
		TArray<UEdGraphNode*> DecoGraphNodes;
		for (UEdGraphNode* Sub : GetGraphSubNodes(ChildGraphNode))
		{
			if (IsDecoratorGraphNode(Sub)) DecoGraphNodes.Add(Sub);
		}
		const int32 DecoN = FMath::Min(Child.Decorators.Num(), DecoGraphNodes.Num());
		for (int32 d = 0; d < DecoN; ++d) SetGraphNodeInstance(DecoGraphNodes[d], Child.Decorators[d]);
		if (Child.Decorators.Num() != DecoGraphNodes.Num())
		{
			Stats.Warnings.Add(FString::Printf(TEXT("%s 第 %d 个子节点: decorators 数量不一致 (tree=%d, graph=%d)，仅前 %d 项已同步"),
				*RootTreeNode->GetName(), i, Child.Decorators.Num(), DecoGraphNodes.Num(), DecoN));
		}
	}
}

/** sync_graph 动作的入口：按结构位置重建整棵 Graph 的 NodeInstance 对应关系。 */
static bool RebuildGraphFromRootNode(UBehaviorTree* BT, FString& OutMessage, TArray<FString>& OutWarnings)
{
	if (!BT || !BT->RootNode)
	{
		OutMessage = TEXT("BT 或 RootNode 为空");
		return false;
	}

	FObjectProperty* GraphProp = FindFProperty<FObjectProperty>(BT->GetClass(), TEXT("BTGraph"));
	UObject* GraphObj = GraphProp ? GraphProp->GetObjectPropertyValue_InContainer(BT) : nullptr;
	UEdGraph* Graph = Cast<UEdGraph>(GraphObj);
	if (!Graph)
	{
		OutMessage = TEXT("该 BT 没有可视化 Graph（从未在编辑器打开过），无需同步");
		return false;
	}

	UEdGraphNode* RootVisual = FindRootVisualGraphNode(Graph);
	if (!RootVisual)
	{
		OutMessage = TEXT("未能在 Graph 中定位到代表 RootNode 的可视化节点（找不到 Root 辅助节点或其连线）");
		return false;
	}

	FGraphSyncStats Stats;
	SyncNodePairRecursive(BT->RootNode, RootVisual, Stats);

	Graph->Modify();
	Graph->MarkPackageDirty();

	OutMessage = FString::Printf(TEXT("已按结构位置同步 %d 个节点的 NodeInstance"), Stats.MatchedCount);
	OutWarnings = Stats.Warnings;
	return true;
}
#endif

/**
 * 从 operations[].properties 读取 [{name,value}] 并通过反射 ImportText 应用到节点上。
 * 单项失败不中断其余项，但会收进 OutErrors 由调用方回给客户端——静默吞掉会让调用方
 * 误以为初值已生效。
 */
static void ApplyInitialProperties(UBTNode* Node, const TSharedPtr<FJsonObject>& OpArgs, TArray<FString>& OutErrors)
{
	if (!Node || !OpArgs.IsValid() || !OpArgs->HasField(TEXT("properties"))) return;

	const TArray<TSharedPtr<FJsonValue>>& PropsArr = OpArgs->GetArrayField(TEXT("properties"));
	UClass* NodeClass = Node->GetClass();
	for (const TSharedPtr<FJsonValue>& PropVal : PropsArr)
	{
		const TSharedPtr<FJsonObject>* PropObjPtr = nullptr;
		if (!PropVal.IsValid() || !PropVal->TryGetObject(PropObjPtr) || !PropObjPtr)
		{
			OutErrors.Add(TEXT("properties 项不是对象，已跳过"));
			continue;
		}
		const TSharedPtr<FJsonObject>& PropObj = *PropObjPtr;

		FString PropName, PropValue;
		if (!PropObj->TryGetStringField(TEXT("name"), PropName) || PropName.IsEmpty())
		{
			OutErrors.Add(TEXT("properties 项缺少非空 name，已跳过"));
			continue;
		}
		if (!PropObj->TryGetStringField(TEXT("value"), PropValue))
		{
			OutErrors.Add(FString::Printf(TEXT("'%s' 缺少 value，已跳过"), *PropName));
			continue;
		}

		FProperty* Prop = NodeClass ? NodeClass->FindPropertyByName(*PropName) : nullptr;
		if (!Prop)
		{
			OutErrors.Add(FString::Printf(TEXT("在 %s 上未找到属性 '%s'"),
				NodeClass ? *NodeClass->GetName() : TEXT("<null class>"), *PropName));
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node);
#if NX_UE_HAS_IMPORT_TEXT_DIRECT
		const bool bOk = Prop->ImportText_Direct(*PropValue, ValuePtr, Node, PPF_None) != nullptr;
#else
		const bool bOk = Prop->ImportText(*PropValue, ValuePtr, PPF_None, Node) != nullptr;
#endif
		if (!bOk)
		{
			OutErrors.Add(FString::Printf(TEXT("设置 '%s' = '%s' 失败（ImportText 失败）"), *PropName, *PropValue));
		}
	}
}

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
	Out.Description = TEXT("批量编辑 BT 节点/装饰器/服务。replace_node 就地换类型，sync_graph 修复图错位；写前关闭已开的编辑器 Tab。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),      FNexusSchema::Enum(TEXT("操作类型"),
			{ TEXT("set_root"), TEXT("add_node"), TEXT("remove_node"), TEXT("replace_node"), TEXT("move_node"),
			  TEXT("add_decorator"), TEXT("remove_decorator"),
			  TEXT("add_service"),   TEXT("remove_service"),
			  TEXT("set_blackboard"), TEXT("set_property"), TEXT("sync_graph") }))
		.Prop(TEXT("nodeClass"),   FNexusSchema::Str(TEXT("节点类名（set_root/add_node/replace_node/add_decorator/add_service）")))
		.Prop(TEXT("nodeName"),    FNexusSchema::Str(TEXT("显示名覆盖（可选）")))
		.Prop(TEXT("parentPath"),  FNexusSchema::Str(TEXT("从根起的点分子节点索引，如 '' 或 '0.1'")))
		.Prop(TEXT("childIndex"),  FNexusSchema::Int(TEXT("子槽索引（add_node/move_node/装饰器/服务）"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("targetPath"),  FNexusSchema::Str(TEXT("目标节点点分路径（remove_node/replace_node/move_node/set_property）")))
		.Prop(TEXT("targetIndex"), FNexusSchema::Int(TEXT("decorators[]/services[] 中要删改的索引"), TNumericLimits<int64>::Min(), 0))
		.Prop(TEXT("blackboardPath"), FNexusSchema::Str(TEXT("BlackboardData 资产路径（set_blackboard）")))
		.Prop(TEXT("targetType"),  FNexusSchema::Enum(TEXT("set_property 的目标类型"),
			{ TEXT("node"), TEXT("decorator"), TEXT("service") }))
		.Prop(TEXT("propertyName"),  FNexusSchema::Str(TEXT("要设置的 UPROPERTY 名（set_property）")))
		.Prop(TEXT("propertyValue"), FNexusSchema::Str(TEXT("文本值，ImportText 格式（set_property）")))
		.Prop(TEXT("properties"),  FNexusSchema::ArrOfObj(TEXT("add_node/replace_node 初始属性 [{name,value}]")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("行为树资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("bt"), TEXT("node"), TEXT("decorator"), TEXT("service"), TEXT("blackboard"),
		TEXT("replace"), TEXT("graph"), TEXT("sync")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_behavior_tree"), TEXT("manage_asset_blackboard"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删/替换/移动节点、装饰器、服务、设属性；图与运行时树错位时用 sync_graph");
}

FCapabilityResult FManageAssetBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath);
		if (!BT)
		{
			OutError = FString::Printf(TEXT("BehaviorTree 未找到: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0) { OutError = TEXT("缺少 operations 或为空"); return; }

#if WITH_EDITOR
		// 写操作前先关闭该资产可能已打开的编辑器 Tab，避免其旧的可视化 Graph 之后把这里的直接修改冲正掉。
		// 关闭不保存，会丢弃编辑器里的未保存改动，因此必须回报给调用方。
		if (CloseOpenEditorToAvoidGraphOverwrite(BT))
		{
			OutTop->SetBoolField(TEXT("editorClosed"), true);
		}
#endif

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);

		FString Action;
		if (!OpArgs->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			Entry->SetStringField(TEXT("error"), TEXT("缺少 action"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}
		Action.ToLowerInline();

		Entry->SetStringField(TEXT("action"), Action);

		// ── set_root ───────────────────────────────────────────────────────────────
		if (Action == TEXT("set_root"))
		{
			FString NodeClass;
			if (!OpArgs->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_root 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class || !Class->IsChildOf(UBTCompositeNode::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 未找到或不是 BTCompositeNode 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTCompositeNode* NewRoot = NewObject<UBTCompositeNode>(BT, Class);
			FString NodeName;
			if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				NewRoot->NodeName = NodeName;
			}
			BT->RootNode = NewRoot;
			BT->MarkPackageDirty();
			Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
		}
		// ── add_node ───────────────────────────────────────────────────────────────
		else if (Action == TEXT("add_node"))
		{
			FString NodeClass;
			if (!OpArgs->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_node 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("nodeClass '%s' 未找到"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
			const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
			if (!bIsComposite && !bIsTask)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 须为 BTCompositeNode 或 BTTaskNode 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FString ParentPath;
			OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FBTCompositeChild NewChild;
			NewChild.ChildComposite = nullptr;
			NewChild.ChildTask = nullptr;
			UBTNode* CreatedNode = nullptr;
			if (bIsComposite)
			{
				UBTCompositeNode* NewComp = NewObject<UBTCompositeNode>(BT, Class);
				FString NodeName;
				if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
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
				if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
				{
					NewTask->NodeName = NodeName;
				}
				NewChild.ChildTask = NewTask;
				CreatedNode = NewTask;
			}

			// 支持在创建节点时直接设置初始属性（避免后续 set_property 因类卸载而崩溃）
			TArray<FString> PropErrors;
			ApplyInitialProperties(CreatedNode, OpArgs, PropErrors);
			ReportPropertyErrors(Entry, PropErrors);

			const int32 InsertIdx = [&]() -> int32
			{
				if (OpArgs->HasField(TEXT("childIndex")))
				{
					const int32 Idx = static_cast<int32>(OpArgs->GetNumberField(TEXT("childIndex")));
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
		// ── move_node ──────────────────────────────────────────────────────────────
		else if (Action == TEXT("move_node"))
		{
			FString TargetPath;
			if (!OpArgs->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("move_node 需要 targetPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTCompositeNode* SrcParent = nullptr;
			int32 SrcIdx = INDEX_NONE;
			if (!FindParentAndIndex(BT, TargetPath, SrcParent, SrcIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 无效"), *TargetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FString NewParentPath;
			OpArgs->TryGetStringField(TEXT("parentPath"), NewParentPath);
			if (NewParentPath.StartsWith(TargetPath + TEXT(".")) || NewParentPath == TargetPath)
			{
				Entry->SetStringField(TEXT("error"), TEXT("不能将节点移动到其自身或子树下"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTCompositeNode* DstParent = FindCompositeByPath(BT, NewParentPath);
			if (!DstParent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *NewParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			const FBTCompositeChild MovedChild = SrcParent->Children[SrcIdx];
			SrcParent->Children.RemoveAt(SrcIdx);

			const int32 InsertIdx = [&]() -> int32
			{
				if (OpArgs->HasField(TEXT("childIndex")))
				{
					const int32 Idx = static_cast<int32>(OpArgs->GetNumberField(TEXT("childIndex")));
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
		// ── remove_node ────────────────────────────────────────────────────────────
		else if (Action == TEXT("remove_node"))
		{
			FString TargetPath;
			if (!OpArgs->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_node 需要 targetPath；用 set_root 替换根节点"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTCompositeNode* Parent = nullptr;
			int32 ChildIdx           = INDEX_NONE;
			if (!FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 无效"), *TargetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Parent->Children.RemoveAt(ChildIdx);
			BT->MarkPackageDirty();
			Entry->SetStringField(TEXT("removedPath"), TargetPath);
		}
		// ── replace_node ───────────────────────────────────────────────────────────
		// 原子操作：就地替换某个已有子节点的类型，只改 FBTCompositeChild 里的
		// ChildComposite/ChildTask 指针，不动 Children 数组长度/下标。
		// 相比 remove_node + add_node 拼接，好处是：
		//   1) 不会因为数组下标变化而导致同批次里后续目标路径失效/改错节点；
		//   2) 原槽位上挂载的 decorators/services 自动保留（它们存在 FBTCompositeChild 上，不随节点类型变化）。
		else if (Action == TEXT("replace_node"))
		{
			FString TargetPath;
			if (!OpArgs->TryGetStringField(TEXT("targetPath"), TargetPath) || TargetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("replace_node 需要 targetPath；替换根节点请用 set_root"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FString NodeClass;
			if (!OpArgs->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("replace_node 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("nodeClass '%s' 未找到"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const bool bIsComposite = Class->IsChildOf(UBTCompositeNode::StaticClass());
			const bool bIsTask      = Class->IsChildOf(UBTTaskNode::StaticClass());
			if (!bIsComposite && !bIsTask)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 须为 BTCompositeNode 或 BTTaskNode 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTCompositeNode* Parent = nullptr;
			int32 ChildIdx           = INDEX_NONE;
			if (!FindParentAndIndex(BT, TargetPath, Parent, ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 无效"), *TargetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FBTCompositeChild& Slot = Parent->Children[ChildIdx];
			const bool bOldIsComposite = Slot.ChildComposite != nullptr;
			if (bOldIsComposite != bIsComposite)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("节点类型不匹配：原节点是 %s，新类是 %s，两者必须同为 Composite 或同为 Task"),
					bOldIsComposite ? TEXT("Composite") : TEXT("Task"),
					bIsComposite ? TEXT("Composite") : TEXT("Task")));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
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
				if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
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
				if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
				{
					NewTask->NodeName = NodeName;
				}
				Slot.ChildTask = NewTask;
				CreatedNode = NewTask;
			}

			TArray<FString> PropErrors;
			ApplyInitialProperties(CreatedNode, OpArgs, PropErrors);
			ReportPropertyErrors(Entry, PropErrors);
			BT->MarkPackageDirty();

#if WITH_EDITOR
			const bool bGraphSynced = SyncEdGraphNodeInstance(BT, OldNode, CreatedNode);
			Entry->SetBoolField(TEXT("graphSynced"), bGraphSynced);
#endif

			Entry->SetStringField(TEXT("replacedPath"),   TargetPath);
			Entry->SetStringField(TEXT("oldNodeClass"),   OldNodeClassName);
			Entry->SetStringField(TEXT("nodeClass"),      Class->GetName());
			if (MovedChildren > 0) Entry->SetNumberField(TEXT("movedChildren"), MovedChildren);
			if (MovedServices > 0) Entry->SetNumberField(TEXT("movedServices"), MovedServices);
		}
		// ── add_decorator ──────────────────────────────────────────────────────────
		else if (Action == TEXT("add_decorator"))
		{
			FString NodeClass;
			if (!OpArgs->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_decorator 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class || !Class->IsChildOf(UBTDecorator::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 未找到或不是 BTDecorator 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FString ParentPath;
			OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			int32 ChildIdx = 0;
			if (OpArgs->HasField(TEXT("childIndex")))
			{
				ChildIdx = (int32)OpArgs->GetNumberField(TEXT("childIndex"));
			}
			if (!Parent->Children.IsValidIndex(ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("childIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTDecorator* Dec = NewObject<UBTDecorator>(BT, Class);
			FString NodeName;
			if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
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
		// ── remove_decorator ───────────────────────────────────────────────────────
		else if (Action == TEXT("remove_decorator"))
		{
			FString ParentPath;
			OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			int32 ChildIdx = 0;
			if (OpArgs->HasField(TEXT("childIndex"))) ChildIdx = (int32)OpArgs->GetNumberField(TEXT("childIndex"));
			if (!Parent->Children.IsValidIndex(ChildIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("childIndex %d out of range [0, %d)"), ChildIdx, Parent->Children.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			int32 TargetIdx = 0;
			if (OpArgs->HasField(TEXT("targetIndex"))) TargetIdx = (int32)OpArgs->GetNumberField(TEXT("targetIndex"));
			auto& Decs = Parent->Children[ChildIdx].Decorators;
			if (!Decs.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("targetIndex %d out of range [0, %d)"), TargetIdx, Decs.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Decs.RemoveAt(TargetIdx);
			BT->MarkPackageDirty();
			Entry->SetStringField(TEXT("parentPath"), ParentPath);
			Entry->SetNumberField(TEXT("childIndex"), ChildIdx);
			Entry->SetNumberField(TEXT("removedIndex"), TargetIdx);
		}
		// ── add_service ────────────────────────────────────────────────────────────
		else if (Action == TEXT("add_service"))
		{
			FString NodeClass;
			if (!OpArgs->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_service 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(NodeClass);
			if (!Class || !Class->IsChildOf(UBTService::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("nodeClass '%s' 未找到或不是 BTService 子类"), *NodeClass));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FString ParentPath;
			OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBTService* Svc = NewObject<UBTService>(BT, Class);
			FString NodeName;
			if (OpArgs->TryGetStringField(TEXT("nodeName"), NodeName) && !NodeName.IsEmpty())
			{
				Svc->NodeName = NodeName;
			}
			const int32 AddedIdx = Parent->Services.Add(Svc);
			BT->MarkPackageDirty();

			Entry->SetStringField(TEXT("nodeClass"),  Class->GetName());
			Entry->SetStringField(TEXT("parentPath"), ParentPath);
			Entry->SetNumberField(TEXT("addedIndex"), AddedIdx);
		}
		// ── remove_service ─────────────────────────────────────────────────────────
		else if (Action == TEXT("remove_service"))
		{
			FString ParentPath;
			OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
			UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
			if (!Parent)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("parentPath '%s' is not a composite node or does not exist"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			int32 TargetIdx = 0;
			if (OpArgs->HasField(TEXT("targetIndex"))) TargetIdx = (int32)OpArgs->GetNumberField(TEXT("targetIndex"));
			if (!Parent->Services.IsValidIndex(TargetIdx))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("targetIndex %d out of range [0, %d)"), TargetIdx, Parent->Services.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Parent->Services.RemoveAt(TargetIdx);
			BT->MarkPackageDirty();
			Entry->SetStringField(TEXT("parentPath"),   ParentPath);
			Entry->SetNumberField(TEXT("removedIndex"), TargetIdx);
		}
		// ── set_blackboard ─────────────────────────────────────────────────────────
		else if (Action == TEXT("set_blackboard"))
		{
			FString BBPath;
			if (!OpArgs->TryGetStringField(TEXT("blackboardPath"), BBPath) || BBPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_blackboard 需要 blackboardPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UBlackboardData* BBAsset = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(BBPath);
			if (!BBAsset)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("BlackboardData 未找到: %s"), *BBPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			BT->BlackboardAsset = BBAsset;
			BT->MarkPackageDirty();
			Entry->SetStringField(TEXT("blackboardPath"), BBAsset->GetPathName());
		}
		// ── set_property ───────────────────────────────────────────────────────────
		else if (Action == TEXT("set_property"))
		{
			FString PropertyName;
			if (!OpArgs->TryGetStringField(TEXT("propertyName"), PropertyName) || PropertyName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FString PropertyValue;
			if (!OpArgs->TryGetStringField(TEXT("propertyValue"), PropertyValue))
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyValue"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			// 确定目标类型：node / decorator / service
			FString TargetType = TEXT("node");
			OpArgs->TryGetStringField(TEXT("targetType"), TargetType);

			UBTNode* TargetNode = nullptr;

			if (TargetType == TEXT("node"))
			{
				FString TargetPath;
				OpArgs->TryGetStringField(TEXT("targetPath"), TargetPath);
				TargetNode = FindNodeByPath(BT, TargetPath);
				if (!TargetNode)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetPath '%s' 处未找到节点"), *TargetPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
			}
			else if (TargetType == TEXT("decorator"))
			{
				FString ParentPath;
				OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
				UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
				if (!Parent)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("parentPath '%s' 未找到"), *ParentPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				int32 ChildIdx = 0;
				if (OpArgs->HasField(TEXT("childIndex"))) ChildIdx = (int32)OpArgs->GetNumberField(TEXT("childIndex"));
				if (!Parent->Children.IsValidIndex(ChildIdx))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("childIndex %d out of range"), ChildIdx));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				int32 TargetIdx = 0;
				if (OpArgs->HasField(TEXT("targetIndex"))) TargetIdx = (int32)OpArgs->GetNumberField(TEXT("targetIndex"));
				auto& Decs = Parent->Children[ChildIdx].Decorators;
				if (!Decs.IsValidIndex(TargetIdx))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetIndex %d out of range"), TargetIdx));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				TargetNode = Decs[TargetIdx];
			}
			else if (TargetType == TEXT("service"))
			{
				FString ParentPath;
				OpArgs->TryGetStringField(TEXT("parentPath"), ParentPath);
				UBTCompositeNode* Parent = FindCompositeByPath(BT, ParentPath);
				if (!Parent)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("parentPath '%s' 未找到"), *ParentPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				int32 TargetIdx = 0;
				if (OpArgs->HasField(TEXT("targetIndex"))) TargetIdx = (int32)OpArgs->GetNumberField(TEXT("targetIndex"));
				if (!Parent->Services.IsValidIndex(TargetIdx))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("targetIndex %d out of range"), TargetIdx));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				TargetNode = Parent->Services[TargetIdx];
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown targetType: '%s'"), *TargetType));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			// 通过反射设置属性
			UClass* TargetNodeClass = TargetNode->GetClass();
			if (!TargetNodeClass)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("节点 '%s' 的 GetClass() 返回空（可能类已被卸载）"), *TargetNode->GetName()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FProperty* Prop = TargetNodeClass->FindPropertyByName(*PropertyName);
			if (!Prop)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("在 %s 上未找到属性 '%s'"), *TargetNodeClass->GetName(), *PropertyName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
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
				continue;
			}

			BT->MarkPackageDirty();
			Entry->SetStringField(TEXT("targetType"),    TargetType);
			Entry->SetStringField(TEXT("propertyName"),  PropertyName);
			Entry->SetStringField(TEXT("propertyValue"), PropertyValue);
		}
		// ── sync_graph ─────────────────────────────────────────────────────────────
		// 不依赖旧节点指针，按结构位置（Children/decorators/services 顺序与数量）
		// 整体重建 Graph→RootNode 的 NodeInstance 对应关系。用于修复"之前已经改过
		// RootNode，但编辑器里可视化节点还是旧的"这种历史遗留情况。
		else if (Action == TEXT("sync_graph"))
		{
#if WITH_EDITOR
			FString Msg;
			TArray<FString> Warnings;
			const bool bOk = RebuildGraphFromRootNode(BT, Msg, Warnings);
			Entry->SetBoolField(TEXT("graphSynced"), bOk);
			Entry->SetStringField(TEXT("message"), Msg);
			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarnArr;
				for (const FString& W : Warnings) WarnArr.Add(MakeShared<FJsonValueString>(W));
				Entry->SetArrayField(TEXT("warnings"), WarnArr);
			}
#else
			Entry->SetStringField(TEXT("error"), TEXT("sync_graph 仅在编辑器构建下可用"));
#endif
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		// 所有操作完成后统一通知一次（避免多次 PostEditChange 导致节点指针损坏）
#if WITH_EDITOR
		NotifyBehaviorTreeAssetChanged(BT);
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetBehaviorTreeCapability)
