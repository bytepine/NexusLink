# 独立编辑器工具与领域能力

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

Slate UI、Automation 测试和非阻塞 Live Coding 都属于引擎能力，由 NexusLinkEditor 提供，不要求 NexusLinkExt 或第二个官方 MCP 服务。

## 接口

- `GetEditorSlate`：`Action=Windows|Snapshot|WaitFor`，可传 `Ref`、`MaxDepth`（0–100）。Snapshot 可用 `TextFilter`（文本/提示）与 `TypeFilter`（控件类型）筛选，继续遍历不匹配的容器；最多扫描 20000 个节点、返回 2000 个匹配项。`Nodes` 包含 Ref、Role、Tooltip、Actions 和焦点/勾选状态，Ref 由 NexusLink 自行管理。快照包含截断标记。
- `CaptureEditorSlate`：可传快照中的 `Ref`，空值选择活动窗口；返回本地 `FilePath` 及标准 MCP 图片内容。
- `InteractEditorSlate`：`Action=Click|Type|PressKey|Focus|Hover|Drag|SelectOption|SetChecked|FillForm`，传已查询的 `Ref`。窗口聚焦使用 Windows 查询中的窗口 Ref。

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

新增能力声明操作信封的输出 Schema，搜索详情通过 `outputSchema` 公布，MultiTool 的工具定义也公布。输出在适配和压缩后校验；旧能力未声明时保持既有行为。

发现结果的 `Execution` 包含固定后端及选择原因。输出 Schema 声明窗口、快照节点、截图、逐项测试结果和编译诊断；同时声明 `Nodes_defaults` / `Windows_defaults` / `Tests_defaults`，以便验证最终压缩结果。空窗口标题合法，未知字段或不匹配的嵌套类型会被拒绝。

Slate Ref 绑定到 NexusLink 会话及弱控件引用，快照从当前可见控件树重新读取。界面重建、窗口关闭或控件失效后重新查询 Ref；拒绝隐藏、禁用祖先下的输入目标及断开的窗口控件。

## Slate 进阶操作

`Type(Text, bReplace=true)` 替换文本，空串可清空。`PressKey` 支持 `Ctrl+`、`Shift+`、`Alt+`、`Cmd+` 组合及 Unreal Key 名。替换仅接受标准可编辑文本控件，清除前验证焦点和完整选区，通过引擎按键事件验证完整选区后再清空、输入。

`Hover` 悬停；`Drag` 使用起点 `Ref` 与终点 `EndRef`。`SetChecked(Value="true"|"false")` 接受复选框 Ref。`SelectOption(Value)` 只在该组合框自己的独立弹出窗口内精确匹配可见选项，找不到时不会点击其他同名文字；复用宿主窗口的菜单请用聚焦后的按键导航。所有输入仍通过 NexusLink 的权限检查。

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
