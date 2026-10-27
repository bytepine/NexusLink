# 编辑器工具与 UE 官方 Toolset

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

Slate UI、Automation 测试和非阻塞 Live Coding 都属于引擎能力，由 NexusLinkEditor 提供，不要求 NexusLinkExt 或第二个官方 MCP 服务。

## 接口

- `GetEditorSlate`：`Action=Windows|Snapshot`，可传 `Ref`、`MaxDepth`。兼容后端返回结构化 `Nodes`，官方后端返回带 ref 的 `Snapshot` 文本。快照包含截断标记。
- `CaptureEditorSlate`：可传快照中的 `Ref`，空值选择活动窗口；返回本地 `FilePath`。
- `InteractEditorSlate`：`Action=Click|Type|PressKey|Focus`，传已查询的 `Ref`；输入传 `Text`，按键传 Unreal `Key` 名。窗口聚焦使用 Windows 查询中的窗口 Ref。
- `ControlEditorAutomation`：`Action=Discover|Run|Stop`。发现完成后用 `ListEditorAutomationTests(NameFilter, Limit)` 获取完整名称，运行传 `TestNames[]`，停止传所属运行的 `OperationId`。只使用当前编辑器的本地测试会话。
- `CompileEditorLiveCoding`：启动非等待编译，要求 Windows、构建支持 Live Coding，且用户已启用当前会话。不自动开关，不替代全量编译或编辑器重启。
- `GetEditorOperation(OperationId, WaitSeconds)`：查询本 MCP 会话的操作。默认立即返回，可等待 0–30 秒；未完成仍返回 Running，可继续查询。

长操作信封包含 `OperationId`、`Backend`、`State`、`bCanCancel`、`Message`、`ElapsedSeconds`、可选 `Result`。Completed 表示执行结束；测试应检查逐项结果，编译应检查 `Result.Outcome`。无法可靠判定时为 Unknown，不凭“不再编译”推断成功。结果保留有容量上限，编辑器重启后标识失效。

## 后端与兼容

UE 4.26–5.7 使用 Slate、AutomationController 和 ILiveCodingModule。UE 5.8 可以复用 SlateInspectorToolset、AutomationTestToolset；宿主明确启用这些插件或 AllToolsets 后，重新编译 NexusLink 才会链接官方 Toolset Registry。未安装或未启用时走兼容后端，Backend 标明实际实现。

每个领域在编辑器会话内固定后端。官方后端的禁用、工具过滤或调用失败不会触发兼容回退。首期只开放固定白名单，不接受任意 Toolset/Tool 名。引擎自带插件仍属于 NexusLink；Ext 仅承载第三方插件能力。

Live Coding 各版本都走底层非等待入口。Mac 和不支持 Live Coding 的构建返回明确不可用原因。能力仅面向完整 Editor，不进入独立 Game/DS；Shipping 继续剔除 MCP。

## 权限、等待与输出

UI 输入、测试控制和编译是危险写能力，默认禁用，沿用 Confirm/Custom 模式。它们没有资产事务回滚保证。上述编辑器工具统一使用单 Capability 调用，不接受 `Calls[]` 信封；预检在任何执行前完成。UI 和任务状态不缓存，也不作为断连快照返回。

Desktop、VSCode、Rider 的等待请求支持 MCP `notifications/progress`；客户端须提供进度令牌并接受 SSE，通知只发送给所属请求。数值表示已等待的秒数，不是完成百分比。UE HTTP 直连首期使用状态查询。

标准 `notifications/cancelled` 结束等待，底层操作仍可查询。停止测试须显式请求 Stop，Live Coding 不保证可停止。超时或断连不能作为重新执行写操作的依据。

新增能力声明操作信封的输出 Schema，搜索详情通过 `outputSchema` 公布，MultiTool 的工具定义也公布。输出在适配和压缩后校验；旧能力未声明时保持既有行为。

发现结果的 `Execution` 包含固定后端及选择原因。输出 Schema 声明窗口、快照节点、截图、逐项测试结果和编译诊断；同时声明 `Nodes_defaults` / `Windows_defaults` / `Tests_defaults`，以便验证最终压缩结果。空窗口标题合法，未知字段或不匹配的嵌套类型会被拒绝。

官方 Slate Ref 通过公开 RefCache 绑定到 NexusLink 会话及弱控件引用；缓存只管理标识，快照、截图和输入仍全部经 Registry 执行。兼容后端与官方后端均拒绝隐藏、禁用祖先下的输入目标及断开的窗口控件。

官方快照强制从当前控件树重新渲染，不创建持久观察器，也不解除其他客户端的观察器；快照可能包含源码位置。
