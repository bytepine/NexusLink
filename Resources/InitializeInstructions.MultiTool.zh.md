NexusLink MCP：控制 Unreal Editor 与运行时（**MultiTool 模式**）。已注册的扩展指令片段会追加在本文后。

[English](InitializeInstructions.MultiTool.md) | 中文

GAS：GetRuntimeActorAbilitySystem 增加 Context/Tags/ActivationDiagnostics/ActivationEvents。诊断 section 需要 ActorName；通过 SpecHandle 或 AbilityPath 选择技能。InteractRuntimeActorAbilitySystem 增加 TryActivateGrantedAbility（不授予技能）及 Start/StopActivationCapture。激活前在每个端点启动捕获。bRequestAccepted 不代表权威端确认；检查权威端 Committed/Failed 事件。丢失记录、身份未知、超时均表示未确认。见 docs/gas-retest.md。

> **宿主说明**：**MCP 运行于 Editor 二进制，以及 Development/DebugGame 独立 Game/DS 构建**（`NexusLink` 模块为 `Type: Runtime`；`NexusLinkEditor` 为 `Type: Editor`；Shipping 在编译期剥离服务器）。

## 工具模型

`GetEditorInfo` 仅限 Editor。切换至 Game/DS 后，使用实例发现元数据（projectName/engineVersion/hostKind/netRole/hasPlayWorld）或宿主 `/status` 端点确认身份；不得在该进程复用 Editor 专属调用。

`tools/list` 将**全部已启用能力**作为独立 MCP 工具公开（名称 = 能力名）。

- **直接**使用 `tools/call`，**不得**包装为 `CallCapability(Capability=...)`。
- 此模式**没有** `SearchCapabilities`；依赖每个工具的 `description` / `InputSchema`（含 `[see: ...]`）。
- 元工具：`SubmitFeedback`。

## 第一步（必遵）

连接 UE 后，回答蓝图/Widget/材质/资产问题前，**先调用 MCP**。流程：`SearchAsset`（顶层 `Assets`；指定类型时顶层返回 `RecommendedGet`）→ 再读写。

## 命名与读写（与 SearchMode 相同）

- **Set**：只用于 `*Property`（如 `SetRuntimeActorProperty`）。
- **Interact**：`Action` 命令（`InteractRuntimeWidget`、`InteractRuntimeActorAnimation`）。
- **Manage**：仅 `*Asset*` 结构编辑。
- **禁止**：`ManageAnimation`、`SetRuntimeActorAnimation`。

## 决策规则

1. **读与写**：`get/list/search` 只读；`manage/set/create/delete` 写资产/属性；**`Interact*` 写运行时命令状态**。写成功没有 `success:true`；检查 MCP `isError` 和结构化操作/section/编译/保存错误。单次调用的部分修改可能残留；不得盲目重试写入。
2. **资产与运行时**：`*Asset*` = 编辑器磁盘；`*Runtime*` 需要 PIE。
3. **单目标**：每个工具用 `AssetPath`/`ActorName`/`WidgetName`；`InteractRuntimeWidget` 也接受 `ListRuntimeWidgets` → `Path` 返回的精确 `WidgetPath`。名称有歧义时返回 `Candidates[].Path` 而不交互；脱离层级/隐藏控件会被拒绝。跨目标使用多次 `tools/call`（本模式无 `CallCapability.Calls[]`）。同一目标用 `Sections`/`PropertyPaths`/`Operations`/`Updates`。**SearchAsset 必须收窄**；优先采用返回的 `RecommendedGet` / `RecommendedManage`（指定类型时在顶层）+ `Assets[].Path`。列表带 `<k>_defaults` 时，缺失字段等于默认值（`merged={**defaults,**entry}`）。
4. **纹理/网格/动画/音频/VFX/关卡资产 get/manage 配对**（未返回 `recommended*` 时的后备）：读取 `GetAssetTexture` / `ManageAssetTexture`；`GetAssetStaticMesh` / `ManageAssetStaticMesh`；`GetAssetSkeletalMesh` / `ManageAssetSkeletalMesh`；`GetAssetAnimSequence` / `ManageAssetAnimSequence`；`GetAssetSkeleton` / `ManageAssetSkeleton`；`GetAssetSoundWave` / `ManageAssetSoundWave`；`GetAssetSoundCue` / `ManageAssetSoundCue`；`GetAssetLevel` / `ManageAssetLevel`（`EditorOnly`）；`GetAssetLevelSequence` / `ManageAssetLevelSequence` / `CreateAssetLevelSequence`；`GetAssetPhysicalMaterial` / `ManageAssetPhysicalMaterial` / `CreateAssetPhysicalMaterial`；`GetAssetStringTable` / `ManageAssetStringTable` / `CreateAssetStringTable`；`GetAssetFont` / `ManageAssetFont`（TTF 用 `ReimportAsset`）；`GetAssetFoliageType` / `ManageAssetFoliageType` / `CreateAssetFoliageType`；`GetAssetMediaSource` / `ManageAssetMediaSource` / `CreateAssetMediaSource`。受依赖门控的能力（Niagara / GAS / StateTree / MVVM / IK / PCG / ControlRig / Paper2D / GeometryCollection / CommonUI / MoviePipeline / Python `GetPythonApi` / `ExecPython`）以 `tools/list` 为准，缺失则跳过。PIE Actor：`ListRuntimeActors`；PIE 动画读 `GetRuntimeActorAnimation`，写 `InteractRuntimeActorAnimation`；音频/Niagara/AI 用 `InteractRuntimeActorAudio` / `_niagara` / `_ai`。
5. **GAS PIE**（需要 GameplayAbilities；`tools/list` 无此能力则跳过）：只读 `GetRuntimeActorAbilitySystem`；激活/应用/授予/Cue/Loose Tag 用 `InteractRuntimeActorAbilitySystem`。
6. **Editor / Game 截图与日志**：`GetEditorContext`（选择、Content Browser 路径、当前编辑器关卡的 `CurrentMap`）；`SearchConsoleVariables`；`CaptureViewport` 捕获 Game/PIE 视口（`Target=pie`）或顶层窗口（`Editor` / `EditorDesktop`）。LevelEditor 面板标签及 Actor `ViewAngle` 截图需要 `CaptureEditorPanel`（仅 Editor；`Target='list'` 枚举面板）。Tag 引用者用 `GetGameplayTags`（`Referencers` + `Tag`）。日志用 `GetOutputLog`（先 `Watch`，下一动作后 `bCollectWatch=true`；错误用 `Preset=Diagnose`）/ `SetLogCaptureFilter`（Editor 和独立 Game 均可）。`ListRuntime*` 前等待 `bHasPlayWorld`。
7. **编辑器图例外**：`GetEditorGraph` 读取图发现/布局/选择/视图；`ControlEditorGraph` 控制原生图导航/视图/选择。图数据编辑仍由领域 `ManageAsset*` 承担；通用 Slate 仅作为最后的 UI 后备。

## 参数契约（破坏性变更）

与 SearchMode 相同：不接受 `assetPaths`/`actorNames`/`widgetNames`；manage 仅用 `Operations[]`；get 仅用 `PropertyPaths[]`；`newPath`→`DestAssetPath`、`blueprintPath`→`AssetPath`、`ownerWidget`→`OwnerClass`、`FilePath`→`ScriptPath`、`classPath`→`ClassName`。旧键 → `ArgInvalid`。

## 蓝图 / GAS

与 SearchMode 相同：编辑图前先 `GetAssetBlueprint`；GAS 图用 `ManageAssetBlueprint`；行为树用 `bSaveToDisk` 或 `SaveAsset` 保存（改类型用 `ReplaceNode`，图偏移用 `SyncGraph`）。manage 可选 `bSaveToDisk`；BP/ABP/WBP 可传 `bCompile`。

## 硬性规则

- 参数必须匹配 `InputSchema`；必填字符串非空。
- **`GetRuntimeActorProperty` 需要非空 `ActorName`**；先调用 `ListRuntimeActors`。
- **`ExecCommand` 需要非空 `Command`**。危险能力在 Confirm 模式下需要 `Reason`；`UserDenied` 后不得重试。
- 不确定此引擎版本是否存在 `unreal.*` API 时，先 `GetPythonApi`；不得在 UE4 猜测 UE5 API，反之亦然。
- `ExecPython` 写入仅在使用 `obj.set_editor_property(...)` 或显式 `obj.modify()` 时可 Undo；普通 `obj.foo = x` **不记录**。检查返回的 `bUndoRecorded`，`false` 表示 Ctrl+Z 不会回滚。
- `GetAsset*/ManageAsset*` 前先 `SearchAsset`，优先采用返回的 `RecommendedGet` / `RecommendedManage`。
- 既有 `Sections=["All"]` 响应仍有效时复用；写入或运行状态变化后重读。`RedundantCall` 仅提示，仍会返回新数据。
- `SubmitFeedback` 触发条件与 SearchMode 相同；**`FeedbackHint` 必须处理**。

编辑器 UI：通过 ControlEditorAssetEditor 打开/聚焦资产（Open/Focus/Close 需 GUI 渲染；Close 拒绝脏资产/共享编辑器，先显式保存；通过 GetEditorContext Sections=[AssetEditors] 列出；Status/bValidateOnly 可 headless）；地图用 ControlEditorLevel。图数据用领域 GetAsset*/ManageAsset*，图视图与选择用 GetEditorGraph / ControlEditorGraph。通用 UI 后备：GetEditorSlate → Ref → InteractEditorSlate / CaptureEditorSlate。本地测试：ControlEditorAutomation / ListEditorAutomationTests。编译：CompileEditorLiveCoding。长任务返回 OperationId；轮询 GetEditorOperation（可选 WaitSeconds ≤30）。Completed 是执行状态，须另查 Outcome。取消请求只取消等待；仅支持的操作才可显式 Stop。禁止自动重试写入，或混入 Calls[] 原子批次。搜索会提供 outputSchema 和 Execution 后端信息。

普通/Widget/Anim/GameplayAbility 蓝图通过 ManageAssetBlueprint Operations=[{Action:Reparent,ParentClass:...}] 重设父类；bCompile/bSaveToDisk 仍须显式给出。离开原父类层级要求操作参数 bAllowPotentialDataLoss=true。只读导航：GetNavigationPath(Start={X,Y,Z},End={X,Y,Z},World=Auto|Editor|Play)；普通 ActorName 可选，不需 AIController，不移动也不重建导航。检查 bNavigationAvailable、bPathFound、bPartial；MaxPoints 仅限制返回点数。GetNavigationPoint 投影一个点。路径查询接受 FilterClass、AgentRadius/AgentHeight、ProjectionExtent、bProjectStart/bProjectEnd；WorldName 区分多个 PIE 世界。显式对齐 MoveTo 的 FilterClass/bProjectEnd/bAllowPartialPath。GetRuntimeActorAI 报告跟踪到的完成状态；InteractRuntimeActorAI Stop 停止移动；接受请求本身不等于到达。

资产编辑补充：GetAssetEditPreview 支持 Reparent/ChangeVariableType 及标量 DataAsset Set、Level SetActorProperty、BP SetComponentProperty。类型变更要求叶子蓝图 + bAllowPotentialDataLoss。RenameLocalVariable 使用 FunctionName/VariableName/NewVariableName。GetAssetBlueprint Component 接收 ComponentName/PropertyPaths/Baseline（ClassDefault|ParentTemplate）；ResetComponentPropertyToDefault 使用明确基准（默认 ParentTemplate）。CompileBlueprint/manage 返回 CompilerMessages，错误阻止保存。GetAssetExportOptions 发现格式；ExportAsset bValidateOnly 绝不写盘，ConflictPolicy=Fail|Unique|Replace 覆盖主路径（附属文件取决于导出器）。

蓝图类变量用 ClassReference<BaseClass> 或 Array<ClassReference<BaseClass>>；拒绝原始 UClass 对象引脚。引用值预览接受对象/类引用，但遍历仅限自身拥有的子对象。导航写入：GetEditorContext CurrentMap.Levels → ManageAssetLevel CreateNavMeshBoundsVolume，显式 BoundsMin/BoundsMax → 保存 → ControlEditorScene BuildNavigation → GetEditorOperation。旧式 Skeleton 重定向：ManageAssetAnimSequence/ManageAssetAnimBlueprint Action=Retarget、TargetSkeleton，先 bValidateOnly；写入时显式提供 AnimationRemaps 与 bAllowPotentialDataLoss。递归处理 Sequence 引用需要 bRetargetReferredAssets；UE5 IK Retargeter 是独立功能。

编辑器领域使用 NexusLink 独立实现。NexusLink 自身不直接链接或调用官方 Toolset/ToolsetCore/ToolsetRegistry，也不导入其 Python 包。保留引擎/领域插件必需的传递依赖。GetProjectToolsets 发现 NexusLink 分组/动作。蓝图 GraphDSL/WriteGraphDSL 使用 UnrealGraphText 节点文本（仅追加）；结构化编辑用 WriteGraphPlan。ManageAssetBlueprint Operations 支持 SplitPin/RecombinePin，参数为 GraphName+NodeId+PinName；DuplicateNodes 使用 GraphName+1–64 个唯一 NodeIds（OffsetX/OffsetY 默认 64）。复制支持平铺 CallFunction/VariableGet/VariableSet/Knot/Comment 节点，仅保留内部连线并返回 NodeIdMap；编辑后重新回读节点/引脚。输入结构体编辑仅保留 Vector/Rotator/Vector2D/LinearColor 默认值；其他输入结构体及无效/非有限拆分默认值在修改前拒绝。

## 输入与诊断

InteractRuntimeInput 用于有界本地玩家 Key/InputAction 注入；凭其所属 OperationId 查询 GetRuntimeInput，必要时显式释放。多人场景通过 ControlPie Status 发现精确 PlayWorlds.WorldPath，世界对象名可能重复。GetRuntimePerformance 读取最近进程耗时；GetProfilerCsv 读取 Saved 下既有文本捕获。GetAssetUserWidget PropertyOverrides/Connections 报告静态证据及不确定性。可选 UE5.9+ GetCommonUIInfo 先发现会话 Players，再读激活/焦点/路由；ControlCommonUIDiagnostics 捕获有界事件。均为独立领域实现。

图操作优先专用 API。GetEditorGraph(AssetPath) 发现图；传入返回的 GraphName 查看节点、布局、SelectedNodeIds 及可用原生视图。GraphName 用返回的图相对路径，歧义短名会被拒绝。NodeId/NodeIds 来自发现结果，不用 Slate Ref 或猜测 GUID。ControlEditorGraph 支持 Open|SetView|ZoomToFit|FocusNode|SelectNodes|ClearSelection，除 Open 外均需 GraphName。ViewLocation={X,Y} 是图空间左上偏移而非中心；正数 Zoom 由引擎量化。SelectNodes 用 NodeIds[] 及 SelectionMode=Replace|Add|Remove。ZoomToFit 可设 bOnlySelection=true。两项能力同步返回，没有 OperationId；bRequestAccepted / bViewDeferred 区分接受请求与延迟视图动画。FocusNode / ZoomToFit 后，在稍后 tick 重新查询 GetEditorGraph。视图/选择需要 GUI 原生图编辑器；结构写入、编译和保存仍由领域 Manage 工具承担。缺少图动作时，先发现领域工具并 SubmitFeedback，不得立即用通用 Slate 拖线、开右键菜单或输入。无领域或专用图 API 时，通用 Slate 才作为最后的 UI 后备；使用返回的 Ref，并重新查询/截图验证。电脑控制仅用于无 MCP 路径的操作，例如原生系统对话框或编辑器外 UI。

EditorBusy (-32002) 包含 error.data.bExecuted=false：等待就绪后再提交被拒绝的请求。模态窗口与慢任务仍受保护，Slate 请求也不例外；不能假定支持模态控制。超时/断连不能证明未执行，不得因此自动重放写入。
