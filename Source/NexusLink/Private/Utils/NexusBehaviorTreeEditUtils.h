// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBehaviorTree;
class UBTNode;
class UBTCompositeNode;
class FJsonObject;

/**
 * BehaviorTree 运行时树写操作与可视化 EdGraph 同步的公共工具，供 manage_asset_behavior_tree 独用
 * （只有 1 个调用方，按上一轮 Utils 降级原则放 Private/Utils）。
 * 按 CapabilitySpec §8.3 用 struct final + 全 static，禁 namespace。
 *
 * 路径导航 / 属性写入在运行时树上即可成立，无需 WITH_EDITOR；EdGraph 可视化同步则必须。
 */
struct FNexusBehaviorTreeEditUtils final
{
	FNexusBehaviorTreeEditUtils() = delete;

	/**
	 * 按点分隔的 childIndex 序列从 BT root 向下定位节点。
	 * 空路径 → root 节点。路径解析失败返回 nullptr。
	 */
	static UBTNode* FindNodeByPath(UBehaviorTree* BT, const FString& Path);

	/** 定位 parentPath 对应的 composite 节点；空路径 = root。 */
	static UBTCompositeNode* FindCompositeByPath(UBehaviorTree* BT, const FString& Path);

	/**
	 * 查找 target 节点的父 composite 及其 childIndex。
	 * targetPath 不能为空（root 无父）。
	 */
	static bool FindParentAndIndex(UBehaviorTree* BT, const FString& TargetPath,
	                               UBTCompositeNode*& OutParent, int32& OutIndex);

	/**
	 * 从 operations[].properties 读取 [{name,value}] 并通过反射 ImportText 应用到节点上。
	 * 单项失败不中断其余项，但会收进 OutErrors 由调用方回给客户端——静默吞掉会让调用方
	 * 误以为初值已生效。
	 */
	static void ApplyInitialProperties(UBTNode* Node, const TSharedPtr<FJsonObject>& OpArgs, TArray<FString>& OutErrors);

#if WITH_EDITOR
	/** Modify + PostEditChange 通知 BT 资产已变更。 */
	static void NotifyBehaviorTreeAssetChanged(UBehaviorTree* BT);

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
	static bool CloseOpenEditorToAvoidGraphOverwrite(UBehaviorTree* BT);

	/**
	 * 将 replace_node 刚创建的新运行时节点同步进可视化 EdGraph：
	 * 在 BT->BTGraph 里找到 NodeInstance == OldNode 的图节点，把它的 NodeInstance 换成 NewNode。
	 * 这样：
	 *   1) 编辑器再次打开该 BT 时，图节点标题/属性面板显示的就是新节点（不再是旧类型）；
	 *   2) 后续在编辑器里保存也不会把 RootNode 冲正回旧节点，因为图上引用的已经是新实例。
	 * 若该 BT 从未在编辑器打开过（没有 BTGraph）或图上找不到对应节点，返回 false——
	 * 这不算错误，只是跳过图同步（下次在编辑器打开时会按当前 RootNode 生成新图）。
	 */
	static bool SyncEdGraphNodeInstance(UBehaviorTree* BT, UBTNode* OldNode, UBTNode* NewNode);

	/**
	 * sync_graph 动作的入口：按结构位置重建整棵 Graph 的 NodeInstance 对应关系。
	 *
	 * SyncEdGraphNodeInstance（用于 replace_node）靠"本次调用中被替换掉的旧节点对象指针"
	 * 去匹配 Graph 节点，只对"未来的替换"有效：如果 RootNode 在更早的调用/会话中就已经被
	 * 改写过（旧对象指针已经丢失，不再挂在 RootNode 树上），就永远匹配不上了。
	 *
	 * 本函数不依赖任何"旧指针"，而是同时按同一套深度优先顺序遍历 RootNode 树和
	 * Graph 树（通过输出 Pin 的连线 + SubNodes 承载 decorator/service），
	 * 逐位置强制把 Graph 节点的 NodeInstance 覆盖成 RootNode 树里当前的真实节点对象。
	 * 因此无论 RootNode 是何时被改写的，只要两棵树的结构（节点数量/子节点顺序）一致，
	 * 都能重新对齐。
	 */
	static bool RebuildGraphFromRootNode(UBehaviorTree* BT, FString& OutMessage, TArray<FString>& OutWarnings);
#endif
};
