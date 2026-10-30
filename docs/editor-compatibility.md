# 编辑器能力的原生与兼容后端

[文档导航](README.md) · [后端选择规则](toolset-backends.md)

UE 5.8 宿主启用了对应官方 Toolset 时，可等价映射的操作通过固定 Registry 路由执行。原生方法被禁用、过滤或执行失败时直接报错，不重新执行兼容写入。其他宿主使用引擎 C++ 编辑器接口；不会自动启用官方 Toolset 或启动第二个 MCP 服务。

## 进阶参数与结构编辑

- `ControlEditorPCG` 的兼容实现支持图参数定义/删除、实例参数覆盖/重置、节点设置写入；`GetEditorPCG` 支持实例列表、实例参数、图参数描述和值、节点设置描述。参数兼容范围为标量数值、Bool、String、Name，需启用 PCG（当前插件支持 UE 5.4+）。图引用建议使用完整对象路径 `/Game/PCG/G.G`。图执行、采样、完整原生节点 Schema 等动作仍需官方 PCGToolset。
- `ControlEditorNiagara` 的兼容实现支持 System 属性、User Variables、Renderer 添加/删除与属性写入；相应查询支持值回读。User Variables 限制为已知数值、Bool、向量、颜色、四元数值结构，拒绝任意 UObject/数据接口。`propertyValues` 是以引擎属性名为键的 JSON 字符串；兼容属性写入在全部字段转换通过后才提交。模块动态输入、Stack 修复和完整原生 Schema 仍需 NiagaraToolsets；既有 `ManageAssetNiagaraSystem` 动作继续可用。
- `Get/ControlEditorAnimation` 的兼容实现支持 Sequence 轨道发现/添加、Binding 列表、Section 列表/添加/删除、范围与 Ease、通道名、Float/Bool/Integer/String 关键帧读写及按帧删除。帧号使用显示帧率的整数帧，转换为引擎 Tick Resolution 后写入。Float 插值支持 Auto/Cubic、Linear、Constant。Control Rig 烘焙、动画层等进阶动作仍需 AnimationAssistantToolset；既有 ControlRig 资产操作可使用 `Get/ManageAssetControlRig`。
- `ManageAssetUserWidget` 的 Move/Wrap/ReplaceWithChild/ReplaceWithTemplate/SetNamedSlotContent 在低版本也提供兼容实现。UE 5.8 继续使用 `FWidgetBlueprintOperationUtils`。兼容替换模板仅处理叶控件/空 Panel，Named Slot 仅处理当前 WidgetTree 中的空槽；跨 Panel 移动后可用 SetSlot 配置新 Slot 布局。拒绝环、占用的单子控件容器和非法宿主。

这些操作沿用现有字段，不接受调用者指定工具类或执行路径。异步接口使用 `GetEditorOperation` 查询；兼容结果位于 `Result.CompatibilityResult`，官方结果位于 `Result.OfficialResult`。未实现的兼容动作明确报错。

## 静态网格生成

`ManageAssetStaticMesh` 新增：

- `GenerateLODs(TrianglePercents)`：1–7 个不含 LOD0 的保留比例，范围 `(0,1]` 且不得递增。
- `RemoveLODs`：保留 LOD0。
- `GenerateConvexCollision(HullCount, MaxHullVertices, HullPrecision)`：有界整数参数；生成后可通过 `GetAssetStaticMesh` 回读。
- `SetNaniteEnabled(bEnabled)`：UE 5+；UE4 明确返回不支持。

UE 5.8 复用 EditorToolset.StaticMeshTools。兼容后端使用 StaticMeshEditorSubsystem，UE4 使用 EditorStaticMeshLibrary。写入沿用已有资产保存流程，不自动保存其他脏资产。

## 编辑器场景

新增 `GetEditorScene(Action=Folders|FolderActors|Selection|Trace)` 和 `ControlEditorScene`，作用于当前编辑器世界。Trace 是 Visibility 通道的单次射线检测，输入 Start/End。FolderActors 可递归读取，Selection 是当前编辑器选择集。

写操作支持文件夹创建/重命名/删除、Actor 文件夹、选择集、静态网格 Actor 合并，以及 UE 5+ 的 Level Instance 创建/编辑/提交。Actor 必须属于当前编辑器世界；合并仅接收唯一的 StaticMeshActor，AssetPath 是引擎合并工具的生成基路径（实际网格名由引擎生成，可从返回 Actor 获取），拒绝已存在的基路径包，默认保留源 Actor。Level Instance 创建引用已有的其他关卡，提交只保存该实例所属关卡包；`bDiscard` 用于丢弃实例编辑。

UE 5.8 的文件夹、Trace、合并和实例创建/编辑复用 SceneTools。选择集、显式创建空文件夹及实例提交保留 C++ 实现；实例提交不调用会保存全部脏包的原生方法。UE4 不提供 Level Instance。

## 蓝图图编辑

`GetAssetBlueprint` 新增 `Subgraph`、`NodeTypes`、`NodeTypePins`、`GraphDSL` 分段，均需 GraphName。Subgraph 以 NodeId 为起点；兼容遍历受 MaxDepth/Limit 限制。NodeTypes 需至少两字符 NameFilter，兼容结果包含已加载 K2 类和可调用函数；兼容 Pin 查询使用 FunctionName/FunctionClass，原生查询使用发现的 NodeTypeId。

`ManageAssetBlueprint` 新增 ArrangeNodes、AddFunctionParameter、RemoveFunctionParameter、SetVariableCategory、SetVariableEditable、WriteGraphPlan、WriteGraphDSL。可映射的 5.8 布局、参数与变量属性操作复用 BlueprintTools；低版本通过已有 BlueprintGraph/BlueprintEditorUtils 接口执行。

WriteGraphPlan 在现有图追加 1–64 个节点、至多 128 条连接。GraphNodes 的每项包含 Id 及既有 AddNode 字段，支持 K2Node_CallFunction、K2Node_VariableGet、K2Node_VariableSet；GraphLinks 使用 From/FromPin/To/ToPin，且只能连接本次计划创建的节点。连接失败会移除本次计划节点，返回错误。成功返回 Id→NodeId 映射，可用既有 graph 分段和新增 Subgraph 回读。

WriteGraphDSL/GraphDSL 使用 UE 5.8 原生 S-expression 协议；低版本使用结构化 GraphPlan。两种格式分别保留各自语义，不执行任意 Python。参数与图结构更改沿用现有编译、保存选项。
