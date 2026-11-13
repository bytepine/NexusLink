NexusLink MCP：控制 Unreal Editor 与运行时（资产 / PIE / UMG / 动画 / AI / 编辑器）。已注册扩展片段会追加在本文后。

[English](InitializeInstructions.SearchMode.md) | 中文

> 宿主说明：**MCP 运行于 Editor 二进制，以及 Development/DebugGame 独立 Game/DS 构建**（`NexusLink` 模块为 `Type: Runtime`；`NexusLinkEditor` 为 `Type: Editor`；Shipping 在编译期剥离服务器）。触发关键字位于代理 `InitializePrefix`。

## 工具模型

`GetEditorInfo` 仅限 Editor。切换至 Game/DS 后，使用实例发现元数据（projectName/engineVersion/hostKind/netRole/hasPlayWorld）或宿主 `/status` 端点确认身份；不得在该进程复用 Editor 专属调用。

元工具：`SearchCapabilities`、`CallCapability`、`SubmitFeedback`（直接 `tools/call`）。其他均为能力，通过 `CallCapability(Capability, Arguments)` 调用；`Arguments` 必须是嵌套对象。

## 第一步

连接 UE 后先 MCP，不得猜测 `/Game/...` 或只搜索仓库。读取资产：`SearchAsset`（收窄 `AssetType` + `PathFilter`）→ `Assets[].Path` + `RecommendedGet`/`RecommendedManage`（指定类型时在顶层，`All` 时在逐项中）→ 再读写。能力未知 → `SearchCapabilities`。业务词仅用于 `SearchAsset`，不用于 `SearchCapabilities`。

## Token 预算

- 响应正文是主要开销：收窄 `Sections`（避免 `All`），从较小 `Limit` 开始；日志用 `GetOutputLog` ≤50，结合 `CategoryFilter` / `Verbosity` / `TextFilter`，需要时用 `Offset` 分页。
- 列表带 `<k>_defaults` 时，缺失字段等于默认值（`merged={**defaults,**entry}`）。
- 诊断日志优先 `Preset=Diagnose`（最新 + ≥warning + `SummaryByCategory`/`ErrorCount` + limit≤50）；或手动 `Order=Newest` + `bIncludeSummary`。复现前记 `LatestSequence`，再用 `SinceSequence` 查增量。条目包含 UTC `Time`（ISO-8601）。只捕获下一动作：`GetOutputLog` 设置 `Watch`（Categories/TextIncludes/TextExcludes/Verbosity），执行动作后 `bCollectWatch=true`。连续重复折叠为 `Repeat`。
- **单目标**：能力用 `AssetPath` / `ActorName` / `WidgetName`；`InteractRuntimeWidget` 也接受 `ListRuntimeWidgets` → `Path` 返回的精确 `WidgetPath`。名称有歧义则返回 `Candidates[].Path` 而不交互；脱离层级/隐藏控件会被拒绝。跨目标可一轮 `CallCapability(Calls=[{Capability,Arguments},…])` 批次；单目标内部保持 `Sections` / `PropertyPaths` / `Operations` / `Updates`。
- 优先一次 `SearchCapabilities` 传 `CapabilityName` 获取完整 `Parameters[]`。

## 参数契约（破坏性变更）

| 规范形式 | 说明 |
|---|---|
| 单目标 + `Calls[]` | 不接受 `assetPaths`/`actorNames`/`widgetNames`；跨目标仅用元工具 `Calls[]` |
| manage → `Operations[]` | 不接受顶层 `Fields`/`Rows`/`Keys`/`Widgets`/`ops` 或合成裸 `Action` |
| get → `PropertyPaths[]` | 写入用 `Updates[].PropertyPath` |
| 重命名 | `newPath`→`DestAssetPath`；`blueprintPath`→`AssetPath`；`ownerWidget`→`OwnerClass`；`FilePath`→`ScriptPath`；`classPath`→`ClassName` |

旧键一律 → `ArgInvalid`，不兼容别名。

## 命名

| 动词 | 用途 |
|---|---|
| `Get` / `List` / `Search` | 只读 |
| `Set` | 仅 `*Property` + `Updates[]` |
| `Interact` | `Action` 命令（不是 PropertyPaths） |
| `Manage` | 仅磁盘 `*Asset*` 结构编辑 |

禁止：`ManageAnimation`、`SetRuntimeActorAnimation`。

## 路由（模式与例外）

**资产**：优先 `SearchAsset` → `RecommendedGet` / `RecommendedManage`；否则 `{Get|Manage|Create}Asset{Type}`，Type ∈ `Blueprint` / `Material` / `AnimBlueprint` / `AnimMontage` / `UserWidget` / `BehaviorTree` / `Blackboard` / `DataTable` / `DataAsset` / `Struct` / `Texture` / `StaticMesh` / `SkeletalMesh` / `AnimSequence` / `Skeleton` / `SoundWave` / `SoundCue` / `Level` / `LevelSequence` / `PhysicalMaterial` / `StringTable` / `FoliageType` / `Font` / `MediaSource`。一个动词+类型覆盖全部子方面，不寻找 `ManageAssetBlueprintVariable` 等工具。例外：`ManageAssetStructField`、`GetAssetRefs`、`ExportAsset`、`ReimportAsset`、`CompileBlueprint`、`SaveAsset` / `RenameAsset` / `DuplicateAsset` / `DeleteAsset` / `UnloadAsset`。

**运行时**：`{Verb}Runtime{Target}{Aspect?}`（`List`/`Get`/`Set`/`Spawn`/`Destroy`/`Interact`/`Diff`；Target=`Actor`/`Widget`/`SlateWidget`，Actor 可加 `Property`/`Animation`/`BehaviorTree`/`Audio`/`Niagara`/`AI`/`AbilitySystem`）。动画读 `GetRuntimeActorAnimation`，写 `InteractRuntimeActorAnimation`。不符合模式的例外：`InteractRuntimeWidget`、`DiffRuntimeActors`、`GetRuntimeSlateWidget`、`GetOutputLog` / `SetLogCaptureFilter`、`ExecCommand`、`CaptureViewport`。

**插件门控**：GAS / Niagara / StateTree / MVVM / EQS / MetaSound / PCG / ControlRig / Enhanced Input / Paper2D / GeometryCollection / CommonUI / Movie Render Queue / Python（`GetPythonApi` / `ExecPython`）仅在插件+引擎匹配时注册；`SearchCapabilities` 返回 `NotFound` 则跳过，不能强行调用握手中的名称。Tag 查询 `GetGameplayTags` 始终可用。

**编辑器例外**：`ControlPie`、`ControlMoviePipeline`、`SearchConsoleVariables`、`CaptureEditorPanel`、`GetEditorContext`（选择 + `CurrentMap`）、`GetEditorInfo`；`GetEditorGraph` 读图发现/布局/选择/视图，`ControlEditorGraph` 控制原生图导航/视图/选择。图数据编辑仍用领域 `ManageAsset*`。

## 工作流说明

GAS 诊断：显式 ActorName；GetRuntimeActorAbilitySystem Sections=[Context,Tags,ActivationDiagnostics]，通过 SpecHandle 或 AbilityPath 指定技能。TryActivateGrantedAbility（绝不授予技能）前，在权威端/客户端 StartActivationCapture；凭 CaptureId 读取 ActivationEvents，最后 StopActivationCapture。bRequestAccepted 仅代表本地接受，必须有权威端 Committed/Failed 证据。身份未知、Dropped>0 或无权威端证据均表示未确认。Diagnose 仅是快照；自定义消耗/检查规则需要真实失败事件。复测见 docs/gas-retest.md。

1. 写蓝图前：`GetAssetBlueprint(Sections=["GraphOverview"])`，使用返回的图名；非 Actor 蓝图禁止 `AddComponent` / `SetDefaults`。BPI：`CreateAssetBlueprint(ParentClass=Interface)`；函数/实现用 `AddFunction` / `AddInterface`。WBP 树/动画用 `ManageAssetUserWidget`（`AddTrack` 可接收 `WidgetName`+`PropertyPath`）；`GetAssetUserWidget(Sections=["GraphOverview"])` 列图，EventGraph 用 `ManageAssetBlueprint`。
2. 行为树：编辑后用 `bSaveToDisk` 或 `SaveAsset`；改类型用 `ReplaceNode`；图偏移用 `SyncGraph`。
3. manage 可选 `bSaveToDisk`（默认 false）；仅 BP/ABP/WBP manage 可传 `bCompile`（默认 false）。独立 `SaveAsset` / `CompileBlueprint` 仍可用。

## 硬性规则

- 写成功没有 `success:true`。检查 MCP `isError` 与结构化操作/section/编译/保存错误；单次调用可能留下部分修改。失败批次提供 `bRolledBack` / `UndoNote`，不得盲目重试整批。`Interact*` 修改运行时命令状态，不用 PropertyPaths。
- `*Asset*` = 磁盘（Editor 宿主）；`*Runtime*` 需要 PIE/Game（`bHasPlayWorld`）；Game/DS 宿主（`HostKind` ≠ Editor）没有 `GetAsset*`。
- `SearchAsset` 禁止 `AssetType=All` + 裸 `/Game/`；之后用 `RecommendedGet` / `RecommendedManage` + `Path`。
- `SearchCapabilities`：按上述模式推导名称，传 `CapabilityName` 一次获取完整 `Parameters[]`；否则用收窄的 1–2 词 `Query`（如 `blueprint variable`）；`Query=""` 是仅名称目录。**禁止**单独使用 `blueprint` / `Asset` / `Runtime` / `Animation`（`QueryTooBroad`）。失败后检查 `ErrorKind` / `SuggestedQueries`；`NotFound` 且提示未注册时不得重试同领域；`CallCapability` 返回 `disabled` 或 `UserDenied` 不得重试。
- `GetRuntimeActorProperty` 需要非空 `ActorName`（`ControlPie`/`GetEditorInfo` 显示 `bHasPlayWorld=true` 后，先 `ListRuntimeActors`）；`ExecCommand` 需要非空 `Command`。标为 `Dangerous` 的能力（`ExecCommand` / `ExecPython` 及后续插件使用同标签的能力）在 Confirm 模式需要 `Reason`（目的、预期效果及为何无更安全的专用能力）；搜索详情会给 `bConfirmationRequired: true`。`UserDenied` 不得重试。
- 不确定此引擎是否存在 `unreal.*` API 时，先 `GetPythonApi`；不得在 UE4 猜测 UE5 API，反之亦然。
- `ExecPython` 写入仅在使用 `obj.set_editor_property(...)` 或显式 `obj.modify()` 时可 Undo；普通 `obj.foo = x` **不记录**。优先前者，并检查返回的 `bUndoRecorded`：`false` 表示 Ctrl+Z 不会回滚。
- 既有 `Sections=["All"]` 响应仍有效时复用；写入或运行状态变化后重读。`RedundantCall` 仅提示，仍返回新数据。
- 重试 ≥2 / 无合适能力 / 猜 Schema / 串行 ≥3 → `SubmitFeedback`；**`FeedbackHint` 必须处理**。

编辑器 UI：通过 ControlEditorAssetEditor 打开/聚焦资产（Open/Focus/Close 需 GUI 渲染；Close 拒绝脏资产/共享编辑器，先显式保存；通过 GetEditorContext Sections=[AssetEditors] 列出；Status/bValidateOnly 可 headless）；地图用 ControlEditorLevel。图数据用领域 GetAsset*/ManageAsset*，图视图与选择用 GetEditorGraph / ControlEditorGraph。通用 UI 后备：GetEditorSlate → Ref → InteractEditorSlate / CaptureEditorSlate。本地测试：ControlEditorAutomation / ListEditorAutomationTests。编译：CompileEditorLiveCoding。长任务返回 OperationId；轮询 GetEditorOperation（可选 WaitSeconds ≤30）。Completed 是执行状态，须另查 Outcome。取消请求只取消等待；仅支持的操作才可显式 Stop。禁止自动重试写入，或混入 Calls[] 原子批次。搜索会提供 outputSchema 和 Execution 后端信息。

普通/Widget/Anim/GameplayAbility 蓝图通过 ManageAssetBlueprint Operations=[{Action:Reparent,ParentClass:...}] 重设父类；bCompile/bSaveToDisk 仍须显式给出。离开原父类层级要求操作参数 bAllowPotentialDataLoss=true。只读导航：GetNavigationPath(Start={X,Y,Z},End={X,Y,Z},World=Auto|Editor|Play)；普通 ActorName 可选，不需 AIController，不移动也不重建导航。检查 bNavigationAvailable、bPathFound、bPartial；MaxPoints 仅限制返回点数。GetNavigationPoint 投影一个点。路径查询接受 FilterClass、AgentRadius/AgentHeight、ProjectionExtent、bProjectStart/bProjectEnd；WorldName 区分多个 PIE 世界。显式对齐 MoveTo 的 FilterClass/bProjectEnd/bAllowPartialPath。GetRuntimeActorAI 报告跟踪到的完成状态；InteractRuntimeActorAI Stop 停止移动；接受请求本身不等于到达。

资产编辑补充：GetAssetEditPreview 支持 Reparent/ChangeVariableType 及标量 DataAsset Set、Level SetActorProperty、BP SetComponentProperty。类型变更要求叶子蓝图 + bAllowPotentialDataLoss。RenameLocalVariable 使用 FunctionName/VariableName/NewVariableName。GetAssetBlueprint Component 接收 ComponentName/PropertyPaths/Baseline（ClassDefault|ParentTemplate）；ResetComponentPropertyToDefault 使用明确基准（默认 ParentTemplate）。CompileBlueprint/manage 返回 CompilerMessages，错误阻止保存。GetAssetExportOptions 发现格式；ExportAsset bValidateOnly 绝不写盘，ConflictPolicy=Fail|Unique|Replace 覆盖主路径（附属文件取决于导出器）。

蓝图类变量用 ClassReference<BaseClass> 或 Array<ClassReference<BaseClass>>；拒绝原始 UClass 对象引脚。引用值预览接受对象/类引用，但遍历仅限自身拥有的子对象。导航写入：GetEditorContext CurrentMap.Levels → ManageAssetLevel CreateNavMeshBoundsVolume，显式 BoundsMin/BoundsMax → 保存 → ControlEditorScene BuildNavigation → GetEditorOperation。旧式 Skeleton 重定向：ManageAssetAnimSequence/ManageAssetAnimBlueprint Action=Retarget、TargetSkeleton，先 bValidateOnly；写入时显式提供 AnimationRemaps 与 bAllowPotentialDataLoss。递归处理 Sequence 引用需要 bRetargetReferredAssets；UE5 IK Retargeter 是独立功能。

编辑器领域使用 NexusLink 独立实现。NexusLink 自身不直接链接或调用官方 Toolset/ToolsetCore/ToolsetRegistry，也不导入其 Python 包。保留引擎/领域插件必需的传递依赖。GetProjectToolsets 发现 NexusLink 分组/动作。蓝图 GraphDSL/WriteGraphDSL 使用 UnrealGraphText 节点文本（仅追加）；结构化编辑用 WriteGraphPlan。ManageAssetBlueprint Operations 支持 SplitPin/RecombinePin，参数为 GraphName+NodeId+PinName；DuplicateNodes 使用 GraphName+1–64 个唯一 NodeIds（OffsetX/OffsetY 默认 64）。复制支持平铺 CallFunction/VariableGet/VariableSet/Knot/Comment 节点，仅保留内部连线并返回 NodeIdMap；编辑后重新回读节点/引脚。输入结构体编辑仅保留 Vector/Rotator/Vector2D/LinearColor 默认值；其他输入结构体及无效/非有限拆分默认值在修改前拒绝。

## 输入与诊断

InteractRuntimeInput 用于有界本地玩家 Key/InputAction 注入；凭其所属 OperationId 查询 GetRuntimeInput，必要时显式释放。多人场景通过 ControlPie Status 发现精确 PlayWorlds.WorldPath，世界对象名可能重复。GetRuntimePerformance 读取最近进程耗时；GetProfilerCsv 读取 Saved 下既有文本捕获。GetAssetUserWidget PropertyOverrides/Connections 报告静态证据及不确定性。可选 UE5.9+ GetCommonUIInfo 先发现会话 Players，再读激活/焦点/路由；ControlCommonUIDiagnostics 捕获有界事件。均为独立领域实现。

图操作优先专用 API。GetEditorGraph(AssetPath) 发现图；传入返回的 GraphName 查看节点、布局、SelectedNodeIds 及可用原生视图。GraphName 用返回的图相对路径，歧义短名会被拒绝。NodeId/NodeIds 来自发现结果，不用 Slate Ref 或猜测 GUID。ControlEditorGraph 支持 Open|SetView|ZoomToFit|FocusNode|SelectNodes|ClearSelection，除 Open 外均需 GraphName。ViewLocation={X,Y} 是图空间左上偏移而非中心；正数 Zoom 由引擎量化。SelectNodes 用 NodeIds[] 及 SelectionMode=Replace|Add|Remove。ZoomToFit 可设 bOnlySelection=true。两项能力同步返回，没有 OperationId；bRequestAccepted / bViewDeferred 区分接受请求与延迟视图动画。FocusNode / ZoomToFit 后，在稍后 tick 重新查询 GetEditorGraph。视图/选择需要 GUI 原生图编辑器；结构写入、编译和保存仍由领域 Manage 工具承担。缺少图动作时，先发现领域工具并 SubmitFeedback，不得立即用通用 Slate 拖线、开右键菜单或输入。无领域或专用图 API 时，通用 Slate 才作为最后的 UI 后备；使用返回的 Ref，并重新查询/截图验证。电脑控制仅用于无 MCP 路径的操作，例如原生系统对话框或编辑器外 UI。

EditorBusy (-32002) 包含 error.data.bExecuted=false：等待就绪后再提交被拒绝的请求。模态窗口与慢任务仍受保护，Slate 请求也不例外；不能假定支持模态控制。超时/断连不能证明未执行，不得因此自动重放写入。
