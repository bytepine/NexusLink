# 独立编辑器工具与领域能力

中文 · [English](editor-toolsets.en.md)

[文档导航](README.md) · [项目首页](../README.md)

专用图编辑器操作、Slate UI、Automation 测试和非阻塞 Live Coding 都属于引擎能力，由 NexusLinkEditor 提供，不要求 NexusLinkExt 或第二个官方 MCP 服务。

## 接口

- `GetEditorGraph`：传 `AssetPath`，省略 `GraphName` 时发现图表；指定返回的 `GraphName` 后读取节点布局、注释、`SelectedNodeIds` 和可用的视图状态。可用 `NodeId`、`NameFilter`、`Offset`、`Limit` 缩小读取范围。
- `ControlEditorGraph`：`Action=Open|SetView|ZoomToFit|FocusNode|SelectNodes|ClearSelection`，通过原生图编辑器 API 操作指定图，不发送鼠标或按键事件，不接收 Slate Ref。节点、引脚和连接的数据修改继续使用各领域 `ManageAsset*`。
- `GetEditorSlate`：`Action=Windows|Snapshot|WaitFor`，可传 `Ref`、`MaxDepth`（0–100）。Snapshot 可用 `TextFilter`（文本/提示）与 `TypeFilter`（控件类型）筛选，继续遍历不匹配的容器；最多扫描 20000 个节点、返回 2000 个匹配项。`Nodes` 包含 Ref、ParentRef、LocalSize、Role、Tooltip、Actions 和焦点/勾选状态，Ref 由 NexusLink 自行管理。`ParentRef` 标识父控件，`LocalSize={X,Y}` 是控件本地尺寸；父项可能被筛选或截断，可按 Ref 查询子树。快照包含截断标记。
- `CaptureEditorSlate`：可传快照中的 `Ref`，空值选择活动窗口；返回本地 `FilePath` 及标准 MCP 图片内容。
- `InteractEditorSlate`：`Action=Click|DoubleClick|Scroll|Type|PressKey|Focus|Hover|Drag|SelectOption|SetChecked|FillForm`，传已查询的 `Ref`。窗口聚焦使用 Windows 查询中的窗口 Ref。快照的通用 `Actions` 包括 Click、DoubleClick、Scroll、Drag、PressKey、Focus 和 Hover，文本框与复选框另提供对应动作；具体结果仍取决于控件是否处理输入。

`Type/bReplace=true` 支持 `SSearchBox` 及普通编辑框。子关卡操作可直接用 `ControlEditorLevel(Action=LoadStreaming|Show|Hide|MakeCurrent, AssetPath=...)`，目标必须是当前地图已有的子关卡引用；LoadStreaming 加载现有引用，MakeCurrent 同时显示并切换当前关卡。
- `ControlEditorAutomation`：`Action=Discover|Run|Stop`。发现完成后用 `ListEditorAutomationTests(NameFilter, Limit)` 获取完整名称，运行传 `TestNames[]`，停止传所属运行的 `OperationId`。只使用当前编辑器的本地测试会话。
- `CompileEditorLiveCoding`：启动非等待编译，要求 Windows、构建支持 Live Coding，且用户已启用当前会话。不自动开关，不替代全量编译或编辑器重启。
- `GetEditorOperation(OperationId, WaitSeconds)`：查询本 MCP 会话的操作。默认立即返回，可等待 0–30 秒；未完成仍返回 Running，可继续查询。

长操作信封包含 `OperationId`、`Backend`、`State`、`bCanCancel`、`Message`、`ElapsedSeconds`、可选 `Result`。Completed 表示执行结束；测试应检查逐项结果，编译应检查 `Result.Outcome`。无法可靠判定时为 Unknown，不凭“不再编译”推断成功。结果保留有容量上限，编辑器重启后标识失效。

## 后端与兼容

UI、测试和编译分别直接使用 Slate、AutomationController 和 ILiveCodingModule；NexusLink 自身在各版本都不直接链接或调用官方 Toolset、Registry 或官方 Python toolset 包。标准模块的[传递依赖遵循引擎要求](toolset-backends.md)。NexusLink 自行管理 Ref、操作归属、参数验证与异步结果。

PCG、Niagara、Control Rig、SemanticSearch 等领域仍需对应领域插件及引擎 API。启用或禁用官方 Toolset 不改变执行路径。Ext 仅承载第三方插件能力。

Live Coding 各版本都走底层非等待入口。Mac 和不支持 Live Coding 的构建返回明确不可用原因。能力仅面向完整 Editor，不进入独立 Game/DS；Shipping 继续剔除 MCP。

## 权限、等待与输出

UI 输入、测试控制、编译、PCG 控制和 Niagara 修复是专用写能力，不带 `Dangerous` 标签；仅任意代码执行入口使用危险能力的 Confirm/Custom 门控。这些异步控制没有资产事务回滚保证。UI、领域诊断与控制保持单条调用；项目设置、插件查询以及技能读取支持通过 `Calls[]` 聚合独立查询。UI、任务状态和图片不缓存，也不作为断连快照返回。

Desktop、VSCode、Rider 的等待请求支持 MCP `notifications/progress`；客户端须提供进度令牌并接受 SSE，通知只发送给所属请求。数值表示已等待的秒数，不是完成百分比。UE HTTP 直连首期使用状态查询。

标准 `notifications/cancelled` 结束等待，底层操作仍可查询。停止测试须显式请求 Stop，Live Coding 不保证可停止。超时或断连不能作为重新执行写操作的依据。

上述长操作能力声明操作信封的输出 Schema，搜索详情通过 `outputSchema` 公布，MultiTool 的工具定义也公布。输出在适配和压缩后校验；旧能力未声明时保持既有行为。

发现结果的 `Execution` 包含固定后端及选择原因。输出 Schema 声明窗口、快照节点、截图、逐项测试结果和编译诊断；同时声明 `Nodes_defaults` / `Windows_defaults` / `Tests_defaults`，以便验证最终压缩结果。空窗口标题合法，未知字段或不匹配的嵌套类型会被拒绝。

Slate Ref 绑定到 NexusLink 会话及弱控件引用，快照从当前可见控件树重新读取。界面重建、窗口关闭或控件失效后重新查询 Ref；拒绝隐藏、禁用祖先下的输入目标及断开的窗口控件。

编辑器操作先使用对应领域 MCP。图表结构和数据使用各领域 `GetAsset*` / `ManageAsset*`，打开、定位、选择及视图操作使用 `GetEditorGraph` / `ControlEditorGraph`。缺少某个图编辑动作时先发现对应专用工具并用 `SubmitFeedback` 记录缺口，不能直接转为鼠标拖线、右键菜单或其他通用 Slate 输入。只有确实没有领域或图表专用接口的界面操作才使用 Slate 兜底；原生系统对话框、编辑器外 UI 等确无 MCP 路径的操作才回退电脑控制。模态窗口或慢任务仍受 `EditorBusy` 门控限制，不保证模态窗口控制。断连或超时后不自动重试写入。

## 专用图表操作

先用 `SearchAsset` 获取资产路径，再用 `GetEditorGraph(AssetPath)` 发现图表。`GraphName` 使用返回的相对路径，不能猜测；同名图存在歧义时会拒绝，改用完整的返回路径。普通蓝图、控件蓝图、动画蓝图及行为树、PCG 等资产内已有的 `UEdGraph` 使用资产所属图；材质和材质函数的已打开编辑器使用其当前预览图，避免把源资产与预览副本混淆。具体可用图以发现结果为准。

指定 `GraphName` 后，`GetEditorGraph` 返回节点标识、坐标、记录的尺寸与注释，可按 `NodeId` 或 `NameFilter` 筛选，并用 `Offset` / `Limit` 分页。`SelectedNodeIds`、`ViewLocation` 和 `Zoom` 描述已存在原生图编辑器的状态；未打开图编辑器时视图不可用，查询不会自动打开图文档。蓝图视图从目标编辑器的唯一图文档取得，避免误读或控制同资产的 Diff 窗口。行为树在 UE 5.3+ 使用所属编辑器的当前图视图，UE 4.26–5.2 仅提供结构读取；PCG 在 UE 5.7 使用所属编辑器的单图标签，UE 5.8+ 按目标图匹配所属编辑器的原生文档，分别读写主图和嵌入子图的视图与选择，`Open` 可打开尚未打开的目标文档。PCG 在 UE 5.4–5.6 仅提供结构读取。具体视图可用性以 `bViewAvailable` 为准。先发现图表和节点，再把返回的 `GraphName`、`NodeId` 或 `NodeIds[]` 交给控制工具。

`ControlEditorGraph` 的 `AssetPath` 必填，除 `Open` 外还需 `GraphName`。动作如下：

- `Open`：省略 `GraphName` 时打开或聚焦资产编辑器并返回图表清单；指定已发现的 `GraphName` 时尝试打开该图。
- `SetView`：至少传 `ViewLocation` 或 `Zoom`。`ViewLocation={X,Y}` 是图坐标的左上偏移，不是视图中心；`Zoom` 范围为 (0,10]，引擎可能将其量化到支持的缩放级别。以再次查询返回的实际值为准。
- `ZoomToFit`：适配全图，`bOnlySelection=true` 时适配当前选择。
- `FocusNode`：用 `NodeId` 定位已发现节点。
- `SelectNodes`：传 1–256 项 `NodeIds[]`，`SelectionMode=Replace|Add|Remove` 指定替换、追加或移除选择，默认 Replace；全部目标验证后再改变选择。
- `ClearSelection`：清空指定图的节点选择。

这两个工具同步返回，不创建 `OperationId`；`bRequestAccepted` / `bViewDeferred` 区分请求已接受与视图仍需延迟更新。视图和选择操作需要 GUI 与可用的原生图编辑器。`FocusNode` / `ZoomToFit` 由引擎延迟移动视图，后续用 `GetEditorGraph` 读回，不把同步返回当作动画已完成。这些工具不执行任意脚本，不改写图结构，不代替各领域的编译、保存和 Undo 契约；仍需修改节点、引脚、连接或布局数据时使用相应 `ManageAsset*`。

### 蓝图结构与布局

蓝图数据先用 `GetAssetBlueprint(Sections=["graph"], GraphName=...)` 读取；节点包含 `PosX` / `PosY` / `Width` / `Height` / `Comment`，引脚包含 `bHidden`、子引脚的 `ParentPin` 和父引脚的 `SubPins[]`。以当前回读的节点与引脚标识调用 `ManageAssetBlueprint.Operations[]`：

- `SplitPin` / `RecombinePin`：必填 `GraphName`、`NodeId`、`PinName`。按 K2 schema 拆分或合并结构体引脚；输入结构体仅支持能保留默认值的 Vector / Rotator / Vector2D / LinearColor，其他输入类型在修改前拒绝，输出结构体仍按 schema 支持范围处理。受支持输入保留合法数值原文，避免大坐标和微小小数被 float 转换或定长小数格式化截断；合并采用当前子引脚值。拆分拒绝已拆分、连线或不可拆分的引脚，拆分和合并都在修改前拒绝非法、非有限或超出对应数值类型范围的默认值；合并可传父或子引脚名，但其后代有连接时拒绝。返回父引脚名及更新后的完整 `Node`，继续编辑前使用新引脚列表。
- `DuplicateNodes`：必填 `GraphName` 与同图 1–64 个唯一 `NodeIds[]`；`OffsetX` / `OffsetY` 默认为 64，可为 0 或负数，位置超出 int32 范围时拒绝。支持原生 CallFunction、VariableGet、VariableSet、Knot 和 Comment 节点；事件、图边界、Timeline、Composite 等资源或结构节点不支持。保留复制集合内部连接，外部连接不复制；新节点使用新 GUID，返回 `NodeIdMap={旧GUID:新GUID}` 与 `Nodes[]`。

连接、断开、节点属性和位置继续使用已有领域操作，例如 `Connect` / `Disconnect`、`SetNode` 与 `ArrangeNodes`。编译与落盘仍须显式传 `bCompile` / `bSaveToDisk`，或调用 `CompileBlueprint` / `SaveAsset`。

## Slate 进阶操作

`Type(Text, bReplace=true)` 替换文本，空串可清空。`PressKey` 支持 `Ctrl+`、`Shift+`、`Alt+`、`Cmd+` 组合及 Unreal Key 名。替换仅接受标准可编辑文本控件，清除前验证焦点和完整选区，通过引擎按键事件验证完整选区后再清空、输入。

`Click` 单击，`DoubleClick` 双击，二者和 `Drag` 均可用 `Button=Left|Right|Middle`（默认 Left）。`Modifiers[]` 可组合 Ctrl、Shift、Alt、Cmd，仅注入事件，不改变系统按键状态；少数通过 `FSlateApplication::GetModifierKeys()` 读取全局状态的控件可能不响应，须执行后读回或截图验证。`Position={X,Y}` 使用目标控件内的归一化坐标，X/Y 均在 [0,1]，默认中心；`Hover` 和 `Scroll` 也可指定该位置。坐标由控件当前几何换算，仍检查目标是否可见、启用和可命中。

`Scroll` 必须传非零 `WheelDelta`，范围 [-20,20]，正值向上、负值向下。`Drag` 必须传起点 `Ref` 与终点 `EndRef`，终点可用 `EndPosition={X,Y}`，范围与默认值同 Position；两个 Ref 可以相同，用于控件内拖动。图表平移和缩放使用 `ControlEditorGraph(SetView)`。输入接受不表示控件已经完成预期操作，执行后重新查询或截图核对。

`SetChecked(Value="true"|"false")` 接受复选框 Ref。`SelectOption(Value)` 只在该组合框自己的独立弹出窗口内精确匹配可见选项，找不到时不会点击其他同名文字；复用宿主窗口的菜单请用聚焦后的按键导航。所有输入仍通过 NexusLink 的权限检查。

`FillForm` 的 `Ref` 指定根，`Fields[]` 每项为 `Ref`、`Value`、`FieldType=textbox|checkbox|combobox`。写入前验证全部字段均为根的可见、启用后代；执行时逐项复检。文本框采用替换。中途失效返回 `AppliedCount`、`FailedFieldIndex`，先前成功项保留，不自动重试。

`WaitFor(Text, TextGone, TimeoutSeconds)` 检查所有可见窗口，不传 Ref；最长 30 秒，使用 ticker 轮询。返回 `bConditionMet` 和 `bTimedOut`，超时属于条件未满足。隐藏子树不匹配，文本遍历有数量和深度上限；截断时不会把“没有发现”当作消失成立。

## UMG 结构编辑

`ManageAssetUserWidget.Operations[]` 新增：

- `Move`：`WidgetName`、`ParentWidget`，可传 `ChildIndex`；同父级也可重排，-1 追加。
- `Wrap`：`WidgetName`、面板 `WidgetClass`，返回 `WrapperName`。
- `ReplaceWithChild`：用单子项替换其容器；`ReplaceWithTemplate` 使用 `WidgetClass` 替换并沿用引擎的引用及 BindWidget 类型检查。
- `SetNamedSlotContent`：为有效空槽创建 `WidgetName` / `WidgetClass`；`SlotName` 必填，`ParentWidget` 是槽宿主，省略时填充继承槽。不会覆盖已占用槽。

先用 `GetAssetUserWidget` 读取 `RootWidget`、各控件 `Parent` / `ChildIndex`、`NamedSlots` 与 `InheritedNamedSlots`。修改父模板后传 `bCompile=true` 或显式 `CompileBlueprint`，再创建派生控件及填充继承槽。写后读回，按需编译和保存。各版本保留既有树、属性与动画操作；结构动作使用该版本 UMGEditor/WidgetTree API，继承槽和模板替换范围按底层引擎支持裁剪。

## 项目设置与插件查询

`GetProjectSettings` 默认返回 Containers，随后按 `Container` 查询 Categories，再带 `Category` 查询 Sections。Properties / Values 还需 `Section`；Values 必须明确给出 1–100 个 `PropertyNames`。只读已注册的设置 UObject，不修改、重置或保存配置；自定义面板没有 UObject 时会明确报错。发现列表支持 `NameFilter`、`Offset`、`Limit`。

`GetProjectPlugins` 默认枚举 Plugins，可按 `NameFilter`、`bEnabledOnly`、分页筛选。带 `PluginName` 查询 Info、Dependencies、Dependents；AssetOwner 以 `AssetPath` 解析项目、引擎或插件挂载根。不会启用、禁用或改写插件描述文件。

## 独立领域 API

下列接口直接调用领域插件的引擎 API，使用 `Backend=Independent` 操作信封。已有 `Result.CompatibilityResult` / `Result.OfficialResult` 可作为历史兼容容器保留，不表示后端选择；具体结构以输出 Schema 为准。Completed 表示调用结束，继续检查业务结果并在写后回读。动作与参数先通过 `GetProjectToolsets(Sections=["Actions"])` 发现。

- **PCG**：`GetEditorPCG` 支持 Instances、GraphStructure、GraphSchema、NativeNodes、NativeNodeSchema、NodeData。图使用资产 object path；ActorPath、NodePath 使用发现结果中的 refPath。NodeData 还可选 PinLabel、AttributeName，Offset / Limit 最多取 1000 个元素。首次读取会启用 inspection 并可能要求重新执行；随后用 `ControlEditorPCG(Action=Execute, ActorPath=...)`，再读数据。Spawn 以 AssetPath、ActorName 和可选 Location 创建实例，默认体积缩放为 25/25/10。所有 PCG 调用串行占用检查状态，另一个调用未结束时返回忙碌原因。`GetAssetPCGGraph` 同时返回输入/输出节点及节点 Path，旧图编辑接口支持连接这些端点并拒绝删除内建端点。
- **Niagara**：`GetEditorNiagara` 查询 CompileState、StackIssues、Dependencies、SystemData、模块拓扑/Schema/值、渲染器及输入 Schema/值、DynamicInputChain、AvailableDynamicInputs。SystemData 用于发现 EmitterName、ScriptName、ModuleName；输入链使用 InputNameStack，渲染器使用 EmitterName / RendererIndex，类级查询使用 ClassPath。`ExecNiagaraStackFix` 只应用同一系统最新 StackIssues 返回的明确 IssueId / FixId，不自动选择修复。
- **SemanticSearch**：`SearchAssetsSemantic` 的 Search 接收 Query，FindSimilar 接收已索引源 AssetPath。两者都要求 ClassPaths，并可传 PathRegexes、Limit（1–100）。使用引擎的混合向量/BM25及相似资产检索，依赖 SemanticSearch 的索引与嵌入提供器；缺少提供器、索引未初始化/为空或源资产未索引时明确报错，不降级为名称匹配。该能力仅依赖 SemanticSearch 领域插件及模型配置，不依赖 SemanticSearchToolset；结果位于 Result.CompatibilityResult.Items，并附带匹配分数与可用的说明文字。
- **项目技能**：`GetProjectSkills` 默认 List，返回 `Result.CompatibilityResult.Skills[]` 中的 Name、Description 与 SkillPath；Details 只接受 List 返回的精确 SKILL.md 路径，增加 Instructions。扫描项目 `Skills/`、`.agents/skills/`、启用插件 `Skills/` 和引擎 `Extras/AgentTools/Skills/`；单文件上限 1 MiB。读取 Instructions 不会自动应用为当前会话指令，不创建或更新文件；旧 AgentSkill 类路径应迁移到 SKILL.md。

## 截图与标准 MCP 图片

`CaptureViewport`、`CaptureEditorPanel`、`CaptureEditorSlate` 默认在 FilePath 元数据之外返回 MCP `content[]` 图片块：`type=image`、`mimeType=image/png|image/jpeg`、base64 `data`。`bIncludeImage=false` 保留仅路径模式。异步 Slate 截图完成后通过所属 GetEditorOperation 取图，批量截图同样保留附件。Text 和 StructuredContent 模式都保留原有元数据。

每个响应最多 4 张、合计 4 MiB 原始图片；超出、文件失效或格式不支持时附带省略说明，路径仍保留。只发送截图实现明确生成的文件，不从任意业务 FilePath 推断附件。同秒截图使用不同文件名。Desktop、VSCode、Rider 对含图片结果不做大文本转存或代理缓存，保证图片可直接显示。

PCG 节点/图/实例参数与子图编辑，以及 PhysicsAsset、GameplayTags 和 DataRegistry 的操作闭环与版本边界见[参数与项目数据](pcg-physics-tags-dataregistry.md)。


后续领域能力见[动画、Niagara 深层写入、Game Features 与自有动作发现](animation-niagara-gamefeatures-toolsets.md)。
