// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusBehaviorTreeEditUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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

UBTNode* FNexusBehaviorTreeEditUtils::FindNodeByPath(UBehaviorTree* BT, const FString& Path)
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

UBTCompositeNode* FNexusBehaviorTreeEditUtils::FindCompositeByPath(UBehaviorTree* BT, const FString& Path)
{
	return Cast<UBTCompositeNode>(FindNodeByPath(BT, Path));
}

bool FNexusBehaviorTreeEditUtils::FindParentAndIndex(UBehaviorTree* BT, const FString& TargetPath,
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

void FNexusBehaviorTreeEditUtils::ApplyInitialProperties(UBTNode* Node, const TSharedPtr<FJsonObject>& OpArgs, TArray<FString>& OutErrors)
{
	if (!Node || !OpArgs.IsValid() || !OpArgs->HasField(TEXT("properties"))) return;

	const TArray<TSharedPtr<FJsonValue>>& PropsArr = OpArgs->GetArrayField(TEXT("properties"));
	UClass* NodeClass = Node->GetClass();
	for (const TSharedPtr<FJsonValue>& PropVal : PropsArr)
	{
		const TSharedPtr<FJsonObject>* PropObjPtr = nullptr;
		if (!PropVal.IsValid() || !PropVal->TryGetObject(PropObjPtr) || !PropObjPtr)
		{
			OutErrors.Add(TEXT("properties item is not an object; skipped"));
			continue;
		}
		const TSharedPtr<FJsonObject>& PropObj = *PropObjPtr;

		FString PropName, PropValue;
		if (!PropObj->TryGetStringField(TEXT("name"), PropName) || PropName.IsEmpty())
		{
			OutErrors.Add(TEXT("properties item missing non-empty name; skipped"));
			continue;
		}
		if (!PropObj->TryGetStringField(TEXT("value"), PropValue))
		{
			OutErrors.Add(FString::Printf(TEXT("'%s' missing value; skipped"), *PropName));
			continue;
		}

		FProperty* Prop = NodeClass ? NodeClass->FindPropertyByName(*PropName) : nullptr;
		if (!Prop)
		{
			OutErrors.Add(FString::Printf(TEXT("Property '%s' not found on %s"),
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
			OutErrors.Add(FString::Printf(TEXT("Failed to set '%s' = '%s' (ImportText failed)"), *PropName, *PropValue));
		}
	}
}

#if WITH_EDITOR

void FNexusBehaviorTreeEditUtils::NotifyBehaviorTreeAssetChanged(UBehaviorTree* BT)
{
	if (!BT) return;
	BT->Modify();
	BT->PostEditChange();
}

bool FNexusBehaviorTreeEditUtils::CloseOpenEditorToAvoidGraphOverwrite(UBehaviorTree* BT)
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

bool FNexusBehaviorTreeEditUtils::SyncEdGraphNodeInstance(UBehaviorTree* BT, UBTNode* OldNode, UBTNode* NewNode)
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

struct FNexusBTGraphSyncStats
{
	int32 MatchedCount = 0;
	TArray<FString> Warnings;
};

/** 深度优先，把 RootTreeNode 及其子树逐位置对齐到 GraphNode 及其可视化子树，强制覆盖 NodeInstance。 */
static void SyncNodePairRecursive(UBTNode* RootTreeNode, UEdGraphNode* GraphNode, FNexusBTGraphSyncStats& Stats)
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
		Stats.Warnings.Add(FString::Printf(TEXT("%s: services count mismatch (tree=%d, graph=%d); synced first %d only"),
			*RootTreeNode->GetName(), Svcs.Num(), ServiceGraphNodes.Num(), SvcN));
	}

	// 子节点：按输出 Pin 连线顺序对齐 Children[] 顺序
	TArray<UEdGraphNode*> ChildGraphNodes = GetGraphOutputChildren(GraphNode);
	const int32 ChildN = FMath::Min(AsComposite->Children.Num(), ChildGraphNodes.Num());
	if (AsComposite->Children.Num() != ChildGraphNodes.Num())
	{
		Stats.Warnings.Add(FString::Printf(TEXT("%s: child count mismatch (tree=%d, graph=%d); synced first %d only"),
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
			Stats.Warnings.Add(FString::Printf(TEXT("%s child %d: decorators count mismatch (tree=%d, graph=%d); synced first %d only"),
				*RootTreeNode->GetName(), i, Child.Decorators.Num(), DecoGraphNodes.Num(), DecoN));
		}
	}
}

bool FNexusBehaviorTreeEditUtils::RebuildGraphFromRootNode(UBehaviorTree* BT, FString& OutMessage, TArray<FString>& OutWarnings)
{
	if (!BT || !BT->RootNode)
	{
		OutMessage = TEXT("BT or RootNode is empty");
		return false;
	}

	FObjectProperty* GraphProp = FindFProperty<FObjectProperty>(BT->GetClass(), TEXT("BTGraph"));
	UObject* GraphObj = GraphProp ? GraphProp->GetObjectPropertyValue_InContainer(BT) : nullptr;
	UEdGraph* Graph = Cast<UEdGraph>(GraphObj);
	if (!Graph)
	{
		OutMessage = TEXT("BT has no visual Graph (never opened in editor); sync not needed");
		return false;
	}

	UEdGraphNode* RootVisual = FindRootVisualGraphNode(Graph);
	if (!RootVisual)
	{
		OutMessage = TEXT("Could not locate visual node for RootNode in Graph (missing Root helper or wires)");
		return false;
	}

	FNexusBTGraphSyncStats Stats;
	SyncNodePairRecursive(BT->RootNode, RootVisual, Stats);

	Graph->Modify();
	Graph->MarkPackageDirty();

	OutMessage = FString::Printf(TEXT("Synced NodeInstance for %d nodes by structure position"), Stats.MatchedCount);
	OutWarnings = Stats.Warnings;
	return true;
}

#endif // WITH_EDITOR
