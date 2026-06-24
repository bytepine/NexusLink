# NexusLink 工具参考手册

本文档列出 NexusLink暴露的全部 MCP 工具与 Capability 的详细参数说明。

> **Tool 与 Capability 的区别**
> - **MCP Tool（3 个）**：`search_capabilities`、`call_capability`、`submit_feedback`。由 AI 直接通过 `tools/call` 调用。
> - **Capability（其余所有）**：通过 `call_capability` 调用：单条 `capability` + `arguments`；或批量 `calls=[{capability,arguments?},...]`。本文档中除元工具节外的所有 `###` 小节均为 Capability，其标题即 `capability` 字段值。
>
> 示例（单条）：`call_capability(capability="list_runtime_actors", arguments={"classFilter":"BP_Enemy"})`

> **`search_capabilities` 失败区分**（看 `errorKind`，勿混读单一 `error` 文案）
> - `not_found`：注册表中无此 Capability 名
> - `disabled`：`capabilityName` / 与 cap 名相同的 `query` 精确查询时，该 cap 已在设置中禁用
> - `disabled_only`：模糊 `query` 无已启用命中，但存在名称匹配的**已禁用** cap（见 `disabledCapabilities[]`）

> **通用约定**
> - 所有 `assetPath` 均为 UE 内容路径，如 `/Game/Blueprints/BP_Player`
> - 属性路径（`propertyPath`）支持 `A.B.C` 点号钻取 + 容器下标 `[i]` / `["key"]`：
>     - 数组：`Items[0]`、`Matrix[0][1]`
>     - Map：`Users["alice"].Score`（Key 与 `ExportText` 结果等值比较；字符串键剥引号）
>     - Set：`Tags["Player.Ally"]`
>     - 组合：`MyComp.RelativeLocation.X`、`Users["alice"].Modules[2].Name`
>     - 路径分段按 `.` 切，Map 的字符串键**不能包含 `.`**
> - 过滤参数支持四种匹配模式：子串（`Player`）、前缀（`^BP_`）、后缀（`Actor$`）、正则（`/^BP_.+$/`）
> - 列表工具均支持 `offset`（默认 0）和 `limit`（默认 100，上限 500）分页
> - 标记 ★ 的参数为必填
> - **批量参数单/复数容错**：所有暴露复数参数（`assetPaths` / `actorNames` / `widgetNames` / `rowNames` / `propertyPaths` / `variables` / `components` / `fields` / `widgets` / `nodes` / `wires` / `operations` / `rows` / `updates` / `spawns` 等）的工具，运行时允许调用方传对应的单数键（`assetPath` / `actorName` / `variable` / `update` ...），会被自动包装成单元素数组继续执行。Schema 仍只宣传复数形态，仅作运行时容错兜底，不要依赖单数形态做新功能设计
> - **响应默认值压缩（全工具默认启用）**：`NexusMcpDispatcher` 在每次工具执行后、序列化前对 `structuredContent` 做一次**递归自动扫描**——遍历所有嵌套层级里的"对象数组"字段 `K`，对其中每个标量字段做分布统计，主流值满足三阈值（`MinCount=3` / `MinMatchRatio=0.7` / `MinNetSaveBytes=30`）时抽到同级 `<K>_defaults`，条目里等于该默认值的同名字段随即省略。内置身份字段排除集（`name` / `path` / `assetPath` / `nodeId` / `tag` / `message` / `timestamp` / `frame` / `id` / `label` / `title` / `text` / `error`）永远不会进入 defaults。
>
>   **消费侧合并规则**：遍历条目时先拿 `<K>_defaults` 作为底，再用条目自身字段覆写（`{**defaults, **entry}`）；条目缺少某字段时视为等于 defaults 值。
>
>   **跳过规则**：同级已出现 `<K>_defaults` 时不二次处理；字段名以 `_defaults` 结尾或等于 `content` 时不参与（协议字段语义冲突）。
>
>   可通过 编辑器 → 编辑器首选项 → 插件 → NexusLink → `响应默认值压缩` 关闭，关闭后工具返回未压缩的原始条目。

---
