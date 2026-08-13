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
> - **单目标 + 跨目标批量（Breaking）**：Capability 仅单目标（`assetPath` / `actorName` / `widgetName`）；跨目标用 `call_capability(calls=[{capability,arguments?},...])`。单目标内集合保留 `sections` / `propertyPaths` / `operations` / `updates`。禁止 `assetPaths`/`actorNames`/`widgetNames` 及旧键（`blueprintPath`→`assetPath`，`newPath`→`destAssetPath`，`ownerWidget`→`ownerClass`，`filePath`→`scriptPath`，Lua `path`→`luaPath`，顶层 `fields`/`rows`/`keys`/`widgets`→`operations`）；旧键 → `arg_invalid`
> - **响应默认值压缩（全工具默认启用）**：`NexusMcpDispatcher` 在每次工具执行后、序列化前对 `structuredContent` 递归扫描所有"对象数组"字段 `K`。仅抽取**全部 object 条目都持有**的标量字段（string / number / bool / null）；主流值满足三阈值（`MinCount=2` / `MinMatchRatio=0.7` / `MinNetSaveBytes=20`）时写入同级 `<K>_defaults`，条目里等值字段随即省略。稀疏字段（仅部分条目写出的键）不抽取，避免 `{**defaults, **entry}` 填错。蓝图 pin / defaults / component 的布尔已改为**始终写出**（`inherited` / `isConst` / `isReference` / `bOrphan` / `bIsNodeEnabled` / `containerType`），以便压缩抽取主流值。身份字段（`name` / `path` / `assetPath` / `nodeId` / `tag` / `message` / `timestamp` / `frame` / `id` / `label` / `title` / `text` / `error`）永不进入 defaults。ForcedDefault：`search_asset` 指定类型抽 `assetType`；`get_output_log` 的 `verbosity≠all` 抽下限；`categoryFilter` / `list_runtime_actors.classFilter` / `list_runtime_widgets.classFilter` 仅当本页实际字段**全员一致**才抽该值（N=1 也抽；禁止把过滤子串当 defaults）。
>
>   **消费侧合并规则**：`merged = {**defaults, **entry}`；条目缺少某字段时视为等于 defaults 值。
>
>   **合并 / 跳过**：同级已有 `<K>_defaults` 时**合并新键、不覆盖已有键**（ForcedDefault 保持权威）；字段名以 `_defaults` 结尾或等于 `content` 时不参与。
>
>   可通过 编辑器 → 编辑器首选项 → 插件 → NexusLink → `响应默认值压缩` 关闭，关闭后返回未压缩原始条目。

---
