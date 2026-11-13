# 输入、性能与 UI 诊断

中文 · [English](runtime-diagnostics.en.md)

这些能力直接使用 UE 领域 API，不依赖官方 Toolset 的注册或调用。可用性由引擎版本、宿主和领域插件决定。

## 编辑器忙碌状态

模态窗口或慢任务期间，普通 MCP 请求返回 JSON-RPC 错误 `-32002`，`error.data` 包含：

- `ErrorKind=EditorBusy`、`Reason=ModalDialogOpen|SlowTaskInProgress`。
- `bExecuted=false`：这条请求未执行。
- `RetryAfterSeconds`：再次检查编辑器状态前的建议等待时间。

`ping`、连接状态和等待取消仍可响应；HTTP `/status` 与 WebSocket `status` 增加 `BusyReason`，空字符串表示未检测到忙碌。取消等待不停止业务操作。

只有明确收到 `bExecuted=false` 的拒绝后，才可在编辑器恢复时重新提交该请求；超时、断连或未知执行结果不能当作未执行，也不能自动重放写入。

UE 5.9 使用按端口的 HTTP 服务接口，在 Slate 模态循环、慢任务进度回调和 Windows 原生模态消息循环中服务本工具的连接；不驱动世界、全局 HTTP 服务或核心 Ticker。旧版本保留执行前拦截，但不保证核心 Ticker 阻塞期间及时响应。macOS 原生系统对话框不保证持续服务。

## 玩家输入

`InteractRuntimeInput` 的 `Action=Apply` 接收 `Inputs[]`、`DurationSeconds`、`WorldPath`（或唯一 WorldName）和 `LocalPlayerIndex`。

- 每项输入二选一：标量 `Key` 或经资产发现得到的 InputAction `AssetPath`。
- `X/Y/Z` 在 [-1,1]，默认 (1,0,0)，至少一个轴非零。数字按键要求 (1,0,0)；Action 的轴数须匹配其类型。
- 同时施加 1–16 项输入，持续 0–30 秒。0 表示至少处理一个真实游戏 Tick 后释放；暂停世界不接受新输入。
- 通过 `ControlPie(Action=Status)` 的 PlayWorlds.WorldPath 发现唯一世界标识；WorldName 只适用于名称唯一的情况。本地玩家索引属于选中的世界；存在多个 Play World 时必须指定精确 `WorldPath`。Dedicated Server 没有本地玩家，不能注入。
- InputAction 持续注入要求启用 EnhancedInput 且 UE 5.7+；旧式标量 Key 使用原生 PlayerInput。

返回的 `OperationId` 属于当前连接会话。用只读 `GetRuntimeInput(OperationId)` 查询操作及仍在保持的 PlayerInput Key／Action 注入状态；用 `InteractRuntimeInput(Action=Release,OperationId)` 提前释放。完成、主动释放、世界清理或服务器停止均清理本工具持有的输入；游戏 Tick 停止后由有限租期兜底。

已有按下状态、其他操作持有的 Key，以及已存在的 Action 注入不会被覆盖。输入状态描述本工具的注入请求，既不表示操作系统物理按键，也不证明角色已经移动或技能已成功；需结合 Actor、Widget 或截图回读验证。

## 实时性能

`GetRuntimePerformance` 返回进程范围的最新可用帧、Game／Render／RHI 线程耗时；单位为毫秒，附采样时刻、帧计数及 `TtlSeconds=0`。线程样本可能来自不同帧，不能视作同步单帧剖析。

GPU 有有效已完成样本时返回 `GpuMs`；否则返回 `bGpuTimingAvailable=false` 和原因。查询不启动录制、不修改控制台配置。此版本不包括 GPU Pass 主动采集和实时 Stat 树。

## CSV 性能查询

`GetProfilerCsv` 在 UE 5.7+ 编辑器可用：

1. `Action=Files` 列出项目 Saved/Profiling 中的 CSV，返回 `CapturePath`。
2. `Action=Summary` 读取该 `CapturePath` 的帧数、指标名与事件数量。
3. `Action=Stats` 配合 `StatFilter`、`FrameStart`、`FrameEnd`，计算 Min／Max／Mean／Total；`SortBy` 可按 Mean／Max／Total 或 Name 排序。
4. `Action=Events` 查询同一帧区间内的事件。

区间为半开 [FrameStart, FrameEnd)，FrameEnd=-1 表示末尾；非法或空区间明确拒绝。Offset／Limit 负责分页。不同指标的单位由捕获文件定义，不能把计数、字节都当作毫秒；非有限样本不参加聚合，返回实际 SampleCount。

仅接受项目 Saved 下的文本 .csv，最多 16 MiB、50 万帧、8192 列、400 万样本。在有界内存快照上使用引擎 CSV 解析器，保留空 `EVENTS` 列、引号及逗号，避免统计列错位；支持尾部完整表头，流式捕获早期尚未出现的尾列按零处理。列数超出表头、重复统计名及无效帧区间明确拒绝。Binary CSV 本版明确不支持；查询不启动录制，也不回传可能包含命令行等信息的全量 Metadata。

## UMG 属性来源与连接

`GetAssetUserWidget` 增加两个 Sections：

- `PropertyOverrides`：按控件查询类默认值、继承 WidgetTree archetype，以及相关绑定／直接图写入声明。PropertyPaths 最多 32 项，默认 Visibility、RenderOpacity、bIsEnabled。
- `Connections`：读取继承链中的旧式属性绑定、MVVM 绑定方向、直接控件绑定事件／委托节点、变量写入和部分 Widget setter 的证据。

NameFilter 筛选两类诊断，TypeFilter 仅筛选 PropertyOverrides 的控件类型；Offset／Limit 对诊断记录分页。结果给出来源对象、蓝图、图和节点 GUID；`LastStaticSource` 只表示最近的静态来源，`bRuntimeWinnerCertain=false` 不推断最终运行值。

分支执行、别名、运行时 handler、任意 C++ setter 和跨函数传播不属于完整证明。声明使用 `DeclaredButUnconfirmed`；默认未启用或不编译的 MVVM 绑定使用 `KnownNotAppliedByDefault`，并保留具体标志。两向和反向 MVVM 绑定按实际方向列出。

## 可选 CommonUI 诊断

仅 UE 5.9+、启用 CommonUI 且包含 CommonUIDebugging 领域模块的 Editor／PIE 宿主注册：

- `GetCommonUIInfo` 默认 Sections=Players，返回会话内玩家索引与稳定 PlayerId。此处索引是整个 PIE 会话范围，区别于输入工具的世界内索引。
- 选择玩家后查询 ActivatableTree、InputRouting、Focus、ActionBindings；按 Limit 限制树前缀或分页列表，截断结果不是完整树。
- `ControlCommonUIDiagnostics(Action=Start)` 启动 1–60 秒有界事件采集，BufferSize 64–4096，默认不采调用栈。返回 OperationId 与 Epoch。
- `Action=Stop` 仅接受当前会话所属 OperationId，不替换或停止其他消费者的日志。
- `GetCommonUIInfo(Sections=[EventLog])` 读取缓冲日志和 DroppedCount；LocalPlayerIndex=-1 可读取已离开玩家的保留事件。轮询使用 Epoch 与 AfterSeq，Epoch 改变必须丢弃旧游标。

日志通过稳定 PlayerId 过滤；事件出现不证明输入最终由某个业务处理器成功处理。采集完成或停止后仍可读保留记录；编辑器模块卸载时清理本工具拥有的采集。MCP 服务停止时也清理所属采集。此版本提供诊断，不自动修复焦点或激活栈。
