# 代理会话层约定

含标准 MCP `content[]` 图片块的结果直接透传，保留 PNG/JPEG MIME、base64 以及既有文本/structuredContent 元数据。三端检测到 `type=image` 时跳过大文本转存和代理缓存；异步及批量调用遵循相同规则。UE 只为截图生成的可信文件创建附件，每次响应最多 4 张、合计 4 MiB 原始图片，省略时仍保留路径及说明。代理不重新解码、转换图片或读取 UE 文件路径。

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

Desktop / Rider / VSCode 夹在 AI 客户端与 UE WebSocket 之间。本文是三端必须对齐的协议补丁；游戏语义仍只在 NexusLink。

参考实现：`nexus-vscode/src/proxy/`。Rider / Desktop 按同一字段与判定对齐。

---

## 1. 职责边界

| 层 | 做 | 不做 |
|---|---|---|
| **代理** | TTL/section 缓存、断线快照、写入前确认、Pause 排队、驾驶舱活动、大包保存到磁盘 | Capability 语义、`SearchCapabilities`、改 UE schema |
| **UE** | 注入 `SnapshotAt` / `TtlSeconds`（若该版本已实现）；危险 cap（`dangerous` 标签）在 Confirm 模式下弹确认框 | 裁 Agent 历史 |

直连 `:45000` **没有**本层。Agent 必须能在缺 `_proxy` 时仍工作。

---

## 2. `_proxy` 对象

代理在 `tools/call` 的 MCP `Result` 上注入（优先写入 `content[0].text` 所解析 JSON 的顶层 `_proxy`；解析失败则挂在 `result._proxy`）：

| 字段 | 类型 | 何时出现 |
|------|------|----------|
| `cache` | `"hit"` \| `"section_hit"` | 未转发 UE，用了会话缓存 |
| `degraded` | `"unavailable"` | UE 不可达，返回上次读快照 |
| `snapshotAt` | ISO-8601 UTC | 快照时间（优先 UE `SnapshotAt`） |
| `offloaded` | `true` | 正文已写入本地文件 |
| `path` | string | 保存到磁盘的绝对路径 |
| `bytes` | number | 保存到磁盘的字节数 |
| `note` | string | 给 Agent 的短提示（固定英文，避免打穿 Prompt Cache 的 initialize；此处是 call 结果） |

`degraded` 时 **禁止** 循环调用 `list_unreal_instances`。`Note` 示例：

`UE editor unreachable (compile/restart?). Serving last snapshot. Do not loop list_unreal_instances.`

写操作在断线时仍返回既有 `ErrorKind: proxy_not_connected`，不提供 degraded 快照。

---

## 3. TTL 缓存键

三端会话策略统一使用当前 PascalCase 契约：`CallCapability` / `Capability` / `Arguments` / `Calls`、`Operations[].Action`、`Sections` 和响应 `SnapshotAt` / `TtlSeconds`。MCP 信封与顶层代理路由字段 `targetHost` / `targetPort` 保持原样。

**实例作用域：** 每条缓存先按最终 UE host/port 隔离；显式目标与当前长连接指向同一地址时共享该作用域。`localhost` / `::1` 归一为 `127.0.0.1`；目标端口只接受 1024–65535 的整数。显式提供非法 `targetPort` 时，三端在查缓存、暂停排队与写入确认之前返回 JSON-RPC `-32602`，不会回落到当前实例。断线保留最近成功连接的作用域；切换实例时禁止复用其他实例的缓存，重连后按实际目标重新计算。调用等待期间发生切换的响应不得污染新实例缓存。

对 `CallCapability`：实例作用域 + `Capability` + 内层 `Arguments`；仅顶层 `targetHost` / `targetPort` 属于代理路由字段，能力参数内的同名字段仍参与键。
MultiTool：工具名即 capability。  
`Calls[]` 批处理：**不缓存**。

**精确命中：** 同一 capability + 规范化 JSON 参数（含 `Sections`）且未过期 → `cache: "hit"`，不转发。

**section 短路：** 同一 capability + 同一 identity（`AssetPath` / `ActorName` / `WidgetName`）下，缓存条目的 `Sections` 含 `"All"` 或为本次 `Sections` 的超集，且距写入 ≤ **30s** → `cache: "section_hit"`（可返回比请求更全的旧 payload）。对应 UE `RedundantCall` 窗口，避免再占 GameThread。

TTL：

- 响应里有 `TtlSeconds` → 用该值
- 否则易过期 cap（`GetOutputLog`、`list_runtime_*`、`get_runtime_*`、`CaptureViewport`）默认 **8s**
- `search_*` 默认 **60s**
- 其余 `get_*` / `list_*` 默认 **30s**
- 写 cap **不缓存**

进程内最多 **64** 条 LRU。成功写操作仅在同一实例作用域内按 identity 失效对应读缓存。

---

## 4. 断线 Continuity

`tools/list`：保持现有行为（断线仍返回上次工具名）。

`tools/call` 读路径且 WS 断开：若有该 cap 的快照（还能用的上次读取结果即使过期也可：`search_*`、`get_asset_*`、`get_editor_*`、`GetAssetRefs`、`GetGameplayTags`），返回快照并打 `degraded: "unavailable"`。

运行时/日志等易过期 cap 过期后 **不** 提供 degraded，走 `proxy_not_connected`。

重连后既有 `tools/list_changed` 刷新清单；读缓存保留直到被写失效或 LRU 淘汰。

---

## 5. 写入前确认与 Pause

配置项 `writeGate`（三端同名语义）：

| 值 | 行为 |
|----|------|
| `off` | 不确认 |
| `destructive`（默认） | `DeleteAsset` / `RenameAsset`；`ControlPie` 且 `Action` 为 stop/end/quit；`manage_*` 且某 `operations[].action` 含 delete/remove/destroy |
| `all` | 一切写 cap（非 `Get*`/`List*`/`Search*`/`SubmitFeedback`） |

确认结果：`allow` / `deny` / `always`（当前 MCP 会话对该 capability 不再问；新 `initialize` 再问）。  
`deny` → JSON-RPC 错误 `ErrorKind: proxy_denied`。  
无 UI 回调时（测试）：不阻塞，视为 `allow`。确认等待上限 **120s**，超时视为 `deny`。

`writeGate: all` 时 `exec_*` / `eval_*` / `dofile_*` 也算写 cap，会**先在代理弹一次**。危险 cap 的代码执行确认在 **UE 里**（直连 `:45000` 也生效）：Editor Preferences 访问模式为「每次手动确认」时由 NexusLink 弹窗，`ErrorKind=UserDenied`。两层同时开会双重确认，建议危险 cap 交给 UE，代理 `writeGate` 保持 `destructive`。

**Pause：** 后续远端 `tools/call` 在代理排队，不发往 UE；本地 `list_unreal_instances` / `connect_unreal_instance` 仍可用。解除后按到达序转发。

---

## 6. 驾驶舱

状态栏 / 托盘展示当前（或最近）一次远端调用：`Capability` + identity 截断。Pause 时显示已暂停。不写入 `initialize.instructions`。

---

## 7. 大包保存到磁盘

`content[0].text` 超过 **48000** 字符时写入系统临时目录 `nexus-mcp-offload/`，结果 JSON 改为摘要 + `_proxy.offloaded` / `path` / `bytes`。Agent 可用 Read 工具打开该路径。

---

## 8. `/status` 与选实例

UE `GET /status` 在 `NetRole` 之外增加：

| 字段 | 含义 |
|------|------|
| `HostKind` | `Editor` / `Game` / `DedicatedServer`。进程级，**不随 PIE 变化** |
| `bHasPlayWorld` | 是否存在 PIE/Game World（与 `list_runtime_*` 探测一致） |

`NetRole` 在编辑器开 PIE 后会变成 `Standalone`/`Client` 等，**不要**再用它判断是不是 Editor 进程。

三端自动连接：`HostKind=Editor` 优先 → 旧 UE 无 `HostKind` 时改用 `NetRole=Editor` → 否则 `found[0]`。`list_unreal_instances` 透出这两个字段。`bHasPlayWorld=false` 时不要调 `list_runtime_*`。
