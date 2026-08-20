# NexusLink 工具参考手册

**语言 / Language**: **简体中文** · [English](tool-reference.md)

本文列出 NexusLink 暴露的全部 MCP 工具与 Capability，并附参数说明。

> **工具 vs Capability**
> - **MCP 工具（3 个）**：`search_capabilities`、`call_capability`、`submit_feedback` — 经 MCP `tools/call` 直接调用。
> - **Capability（其余全部）**：经 `call_capability`：单条 `capability` + `arguments`；或批量 `calls=[{capability,arguments?},...]`。下文除元工具外每个 `###` 小节都是 Capability，标题即 `capability` 字段值。
>
> 示例（单条）：`call_capability(capability="list_runtime_actors", arguments={"classFilter":"BP_Enemy"})`

> **`search_capabilities` 失败类型**（检查 `errorKind`；不要只看一条 `error` 字符串）
> - `not_found`：Capability 名不在注册表
> - `disabled`：精确 `capabilityName` / cap 名 `query` 命中设置中已禁用的 cap
> - `disabled_only`：模糊 `query` 只匹配到**已禁用**的 cap（见 `disabledCapabilities[]`）

> **通用约定**
> - 所有 `assetPath` 均为 UE 内容路径，例如 `/Game/Blueprints/BP_Player`
> - 属性路径（`propertyPath`）支持 `A.B.C` 点分下钻 + 容器下标 `[i]` / `["key"]`：
>     - 数组：`Items[0]`、`Matrix[0][1]`
>     - Map：`Users["alice"].Score`（键与 `ExportText` 比较；字符串键会去掉引号）
>     - Set：`Tags["Player.Ally"]`
>     - 组合：`MyComp.RelativeLocation.X`、`Users["alice"].Modules[2].Name`
>     - 分段按 `.` 切分；Map 的字符串键**不能包含 `.`**
> - 过滤器支持子串（`Player`）、前缀（`^BP_`）、后缀（`Actor$`）、正则（`/^BP_.+$/`）
> - 列表类工具支持 `offset`（默认 0）和 `limit`（默认 100，最大 500）
> - ★ 表示必填参数
> - **manage 收尾**：所有 `manage_asset_*` 接受可选 `saveToDisk`（默认 false）；仅 `manage_asset_blueprint` / `manage_asset_anim_blueprint` / `manage_asset_user_widget` 另接受 `compile`（默认 false）。UDS 改字段后仍会自动编译；材质用 `recompile` 操作。独立的 `save_asset` / `compile_blueprint` 仍可用
> - **单目标 + 跨目标批量（Breaking）**：Capability 一次处理一个目标（`assetPath` / `actorName` / `widgetName`）；跨目标走 `call_capability(calls=[{capability,arguments?},...])`。同一目标内继续用 `sections` / `propertyPaths` / `operations` / `updates`。没有 `assetPaths`/`actorNames`/`widgetNames` 或旧键（`blueprintPath`→`assetPath`、`newPath`→`destAssetPath`、`ownerWidget`→`ownerClass`、`filePath`→`scriptPath`、Lua `path`→`luaPath`、顶层 `fields`/`rows`/`keys`/`widgets`→`operations`）；旧键 → `arg_invalid`
> - **响应默认值压缩（全局开启）**：`NexusMcpDispatcher` 在序列化前递归扫描 `structuredContent` 中的对象数组字段 `K`。仅当标量字段（string / number / bool / null）出现在**每一**条对象上才成为候选；主导值满足阈值（`MinCount=2` / `MinMatchRatio=0.7` / `MinNetSaveBytes=20`）时，在同级写入 `<K>_defaults` 并从条目中省略相等字段。稀疏字段（仅部分条目有）不抽取。身份字段（`name` / `path` / `assetPath` / `nodeId` / `tag` / `message` / `timestamp` / `frame` / `id` / `label` / `title` / `text` / `error`）永不进入 defaults。ForcedDefault：带类型的 `search_asset` 抽取 `assetType`；`get_output_log` 在 `verbosity≠all` 时抽取下限；`categoryFilter` / `list_runtime_actors.classFilter` / `list_runtime_widgets.classFilter` 等过滤器仅当**本页所有条目一致**时抽取（含 N=1；不要把过滤子串当默认值）。
>
>   **合并规则**：`merged = {**defaults, **entry}`；条目缺字段即等于 defaults。
>
>   **合并 / 跳过**：已有 `<K>_defaults` 时合并新键且不覆盖 ForcedDefault；跳过以 `_defaults` 结尾或等于 `content` 的字段名。
>
>   开关：Editor → Editor Preferences → Plugins → NexusLink → `Compact response defaults`。

---
