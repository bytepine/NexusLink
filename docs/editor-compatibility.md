# 独立编辑器能力与版本范围

中文 · [English](editor-compatibility.en.md)

[文档导航](README.md) · [独立后端说明](toolset-backends.md)

这些能力由 NexusLink 自行实现，直接使用引擎编辑器及领域插件 API；本插件自身不直接链接或调用官方 Toolset、Registry 或官方 Python toolset 包。标准模块的[传递依赖遵循引擎要求](toolset-backends.md)。版本限制来自底层引擎 API，动作和参数可通过 `GetProjectToolsets.Sections=["Actions"]` 发现。

## 进阶参数与结构编辑

- `ControlEditorPCG` 的兼容实现支持图参数定义/删除、实例参数覆盖/重置、节点设置写入；`GetEditorPCG` 支持实例列表、实例参数、图参数描述和值、节点设置描述。参数兼容范围为标量数值、Bool、String、Name，需启用 PCG（当前插件支持 UE 5.4+）。图引用建议使用完整对象路径 `/Game/PCG/G.G`。图执行、采样及节点 Schema 使用独立 PCG 实现；具体类型和动作范围按当前引擎 API 返回。
- `ControlEditorNiagara` 的兼容实现支持 System 属性、User Variables、Renderer 添加/删除与属性写入；相应查询支持值回读。User Variables 限制为已知数值、Bool、向量、颜色、四元数值结构，拒绝任意 UObject/数据接口。`propertyValues` 是以引擎属性名为键的 JSON 字符串；兼容属性写入在全部字段转换通过后才提交。模块输入、Stack 诊断与明确修复也由独立 Niagara 实现提供；按动作发现结果核对版本、类型和支持范围。既有 `ManageAssetNiagaraSystem` 动作继续可用。
- `Get/ControlEditorAnimation` 的兼容实现支持 Sequence 轨道发现/添加、Binding 列表、Section 列表/添加/删除、范围与 Ease、通道名、Float/Bool/Integer/String 关键帧读写及按帧删除。帧号使用显示帧率的整数帧，转换为引擎 Tick Resolution 后写入。Float 插值支持 Auto/Cubic、Linear、Constant。Control Rig 烘焙、动画层等进阶动作直接使用当前引擎公开 API；既有 ControlRig 资产操作使用 `Get/ManageAssetControlRig`。
- `ManageAssetUserWidget` 的 Move/Wrap/ReplaceWithChild/ReplaceWithTemplate/SetNamedSlotContent 在低版本也提供兼容实现。UE 5.8 继续使用 `FWidgetBlueprintOperationUtils`。兼容替换模板仅处理叶控件/空 Panel，Named Slot 仅处理当前 WidgetTree 中的空槽；跨 Panel 移动后可用 SetSlot 配置新 Slot 布局。拒绝环、占用的单子控件容器和非法宿主。

这些操作沿用现有字段，不接受调用者指定工具类或执行路径。异步接口使用 `GetEditorOperation` 查询并返回 Backend=Independent。`Result.CompatibilityResult` / `Result.OfficialResult` 可作为历史数据容器保留，具体以输出 Schema 为准。当前引擎不存在的底层动作明确返回版本或类型限制。

## 导航与碰撞诊断

`GetEditorScene` 在当前编辑器世界提供只读动作，结果位于 `Result.CompatibilityResult`：

- `NavigationStatus` 返回 `bNavigationAvailable`、`bNavigationBuilding`、`bBuildingLocked`、`RemainingTasks` 和 `RunningTasks`。可传 `WaitSeconds`（0–30）观察已有构建；返回 `OperationId`，通过 `GetEditorOperation` 查询。超时返回 `bTimedOut=true`，不会发起、停止或重建导航。
- `NavigationContributors` 必须传有限的 `BoundsMin` / `BoundsMax`，查询已存在的导航八叉树，返回所有者/Actor 路径、实际边界、几何与修饰器标记、延迟收集状态及区域类。`Offset` / `Limit` 控制返回条目；不触发延迟几何收集，未建八叉树以 `bOctreeAvailable=false` 表示。
- `NavigationTiles` 使用同样空间范围，查询默认 Recast 导航数据；`ScanLimit` 每次最多检查 65536 个瓦片槽，`NextOffset` 是下次扫描位置，`Limit` 最多返回 500 条。`bTruncated` 表示还有槽未扫描；没有 Recast 数据时返回 `bRecastAvailable=false`。
- `LevelGeometry` 默认读取持久层 `Level.Model` 的 BSP 节点、表面、点和多边形计数、整体及有界节点边界；可用 `AssetPath` 选已有加载关卡包。不会切图、导出或加载隐藏引用。
- `Collision` 要求精确的 `ActorPath` 或 `ComponentPath`，返回组件注册状态、`bPhysicsStateCreated`、`bValidBodyInstance`、碰撞/Visibility 响应及导航标记。`Trace` 可传 `bTraceComplex`，返回命中组件、`FaceIndex`、位置与法线。

`ControlEditorScene.RefreshComponentPhysics` 要求一个精确 `ComponentPath` 和已停止 PIE，调用组件 `RecreatePhysicsState`，不会更改或保存资产属性。导航已经构建或锁定时，`BuildNavigation` 返回 `ErrorKind=StateConflict`；切图因脏包被拒绝时返回同类错误及 `DirtyWorldPackages`，应显式处理所列包。

`GetAssetStaticMesh` 增加 `Navigation`、`Sections`、`LODForCollision` 与 `bContainsPhysicsTriMeshData`。`MaxTriangles`（默认 0，最多 128）、`TriangleOffset`、`LODIndex` 可读取渲染网格的本地三角形和质心；渲染面索引与物理后端的面索引不保证一致，这些字段不能单独证明当前组件的物理状态。`ManageAssetStaticMesh.RefreshCollision` 重建已有 BodySetup 并刷新已注册的网格实例；碰撞复杂度及简单碰撞修改也会同步刷新物理状态和导航组件数据。资产仍由显式保存操作落盘。

`CallCapability(Calls=[...], bAsyncReadOnly=true, TimeoutSeconds=30)` 仅接受编辑器宿主中已注册、允许批量的只读能力，一次 1–64 项，每帧执行一项。`OperationId` 可查询 `CompletedCount`、`TotalCount`、`FailureCount`、`Results` 与进度消息；`ControlEditorOperation(OperationId=...)` 取消尚未执行的读取。截止时间范围为 0.01–120 秒，在条目之间检查，不能中断正在执行的引擎调用。失败、取消和超时保留已完成结果，不重试写入。普通同步 `Calls` 也限制为 64 项。

`ExportAsset` 返回 `ExporterDiagnostics`（最多 32 条原生警告/错误）、`ExporterErrors` 与 World 的 `LevelGeometry` 摘要；无诊断时列表为空，保留导出器、输出路径和 `bWritten` 供核对。`bValidateOnly` 保持不落盘。

## 静态网格生成

`ManageAssetStaticMesh` 新增：

- `GenerateLODs(TrianglePercents)`：1–7 个不含 LOD0 的保留比例，范围 `(0,1]` 且不得递增。
- `RemoveLODs`：保留 LOD0。
- `GenerateConvexCollision(HullCount, MaxHullVertices, HullPrecision)`：有界整数参数；生成后可通过 `GetAssetStaticMesh` 回读。
- `SetNaniteEnabled(bEnabled)`：UE 5+；UE4 明确返回不支持。

UE5 使用 StaticMeshEditorSubsystem，UE4 使用 EditorStaticMeshLibrary；均由 NexusLink 直接调用。写入沿用已有资产保存流程，不自动保存其他脏资产。

碰撞几何坐标使用 Schema 声明的 `X/Y/Z`，或明确的 `ExtentX/Y/Z`、`LocX/Y/Z`。旧参考清单中的小写 `x/y/z` 已纠正；客户端应按 `SearchCapabilities` 返回的字段拼写调用。

## 编辑器场景

新增 `GetEditorScene(Action=Folders|FolderActors|Selection|Trace)` 和 `ControlEditorScene`，作用于当前编辑器世界。Trace 是 Visibility 通道的单次射线检测，输入 Start/End。FolderActors 可递归读取，Selection 是当前编辑器选择集。

写操作支持文件夹创建/重命名/删除、Actor 文件夹、选择集、静态网格 Actor 合并，以及 UE 5+ 的 Level Instance 创建/编辑/提交。Actor 必须属于当前编辑器世界；合并仅接收唯一的 StaticMeshActor，AssetPath 是引擎合并工具的生成基路径（实际网格名由引擎生成，可从返回 Actor 获取），拒绝已存在的基路径包，默认保留源 Actor。Level Instance 创建引用已有的其他关卡，提交只保存该实例所属关卡包；`bDiscard` 用于丢弃实例编辑。

文件夹、Trace、合并、选择集和实例管理均使用独立 C++ 引擎实现；实例提交不调用会保存全部脏包的方法。UE4 不提供 Level Instance。

## 蓝图图编辑

`GetAssetBlueprint` 新增 `Subgraph`、`NodeTypes`、`NodeTypePins`、`GraphDSL` 分段，均需 GraphName。Subgraph 以 NodeId 为起点；兼容遍历受 MaxDepth/Limit 限制。NodeTypes 需至少两字符 NameFilter，兼容结果包含已加载 K2 类和可调用函数；Pin 查询按输入 Schema 使用 FunctionName/FunctionClass 或发现的 NodeTypeId。

`ManageAssetBlueprint` 新增 ArrangeNodes、AddFunctionParameter、RemoveFunctionParameter、SetVariableCategory、SetVariableEditable、WriteGraphPlan、WriteGraphDSL。布局、参数与变量属性操作通过 BlueprintGraph/BlueprintEditorUtils 等引擎接口独立执行。

变量元数据通过 `SetVariableMetadata(VariableName, ToolTip)` 修改，空 ToolTip 清除提示；类别仍使用 `SetVariableCategory`，批量修改不逐项重建骨架。`SetVariableReplication(VariableName, Replication=None|Replicated|RepNotify)` 支持 Actor/ActorComponent 自有成员，拒绝复制 Set/Map；RepNotify 还需 `RepNotifyFunction` 指定已有、无输入输出且非纯函数/RPC 的通知函数（可先 AddFunction）。不会启用所属 Actor/组件的复制，也不会删除已有通知图。`SetFunctionCallInEditor(FunctionName, bCallInEditor)` 配置自有无参函数的编辑器按钮。上述操作均由显式 `bCompile` / `bSaveToDisk` 控制编译和保存；Variable / Function 分段回读 ToolTip、Category、Replication、RepNotifyFunction 和 bCallInEditor。

WriteGraphPlan 在现有图追加 1–64 个节点、至多 128 条连接。GraphNodes 的每项包含 Id 及既有 AddNode 字段，支持 K2Node_CallFunction、K2Node_VariableGet、K2Node_VariableSet；GraphLinks 使用 From/FromPin/To/ToPin，且只能连接本次计划创建的节点。连接失败会移除本次计划节点，返回错误。成功返回 Id→NodeId 映射，可用既有 graph 分段和新增 Subgraph 回读。

GraphDSL/WriteGraphDSL 使用 `UnrealGraphText` 格式，即由引擎 FEdGraphUtilities 导出的原生可复制节点文本。GraphDSL 返回 Code；WriteGraphDSL 将 Code 中 1–500 个可复制节点追加到目标图，写入前在临时完整蓝图中预导入校验。旧官方 Lisp/S-expression 文本不再接受，不提供任意脚本执行。WriteGraphPlan 继续使用上述结构化计划，也没有 GraphPlan 读取分段。参数与图结构更改沿用现有编译、保存选项。
