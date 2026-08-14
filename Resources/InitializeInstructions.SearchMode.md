NexusLink MCP：Unreal 编辑器 + 运行时控制（资产 / PIE / UMG / Lua / 动画 / AI / 编辑器）。

> Host note: **MCP 仅 Editor / PIE**（模块 Type=Runtime，非 Editor 构建 Startup/Shutdown 空返回）。触发关键词见代理 `initializePrefix`。

## 工具模型

元工具：`search_capabilities`、`call_capability`、`submit_feedback`（直接 `tools/call`）。其余为 Capability，经 `call_capability(capability, arguments)`；`arguments` 须为嵌套对象。

## 首要动作

已连接 UE：先 MCP，禁止猜 `/Game/...` 或仅 grep 仓库。读资产：`search_asset`（收窄 `assetType` + `pathFilter`）→ `assets[].path` + `recommendedGet`/`recommendedManage`（指定类型在顶层，`all` 在条目上）→ 再读写。未知 cap → `search_capabilities`。业务词只用于 `search_asset`，不用于 `search_capabilities`。

## Token 预算

- 响应体是主要开销：`sections` 取窄（勿 `all`）、`limit` 从小取；读日志 `get_output_log` ≤50 条并配 `categoryFilter` / `verbosity` / `textFilter`，不够再 `offset` 翻页。
- 列表若含 `<k>_defaults`：缺省字段等于该值（`merged={**defaults,**entry}`）。
- 诊断日志：优先 `preset=diagnose`（newest + ≥warning + `summaryByCategory`/`errorCount` + limit≤50）；或手动 `order=newest` + `includeSummary`；复现前记 `latestSequence`，复现后回传 `sinceSequence` 只取增量。条目含 UTC `time`（ISO-8601）。
- **单目标**：Capability 仅 `assetPath` / `actorName` / `widgetName`；跨目标批量用 `call_capability(calls=[{capability,arguments},…])` 一轮完成。单目标内集合保留 `sections` / `propertyPaths` / `operations` / `updates`。
- `search_capabilities` 优先 `capabilityName` 一次取全 `parameters[]`。

## 参数契约（Breaking）

| 权威 | 说明 |
|---|---|
| 单目标 + `calls[]` | 禁止 `assetPaths`/`actorNames`/`widgetNames`；跨目标只走元工具 `calls[]` |
| manage → `operations[]` | 禁止顶层 `fields`/`rows`/`keys`/`widgets`/`ops`/裸 `action` 合成 |
| get → `propertyPaths[]` | 写操作用 `updates[].propertyPath` |
| 重命名 | `newPath`→`destAssetPath`；`blueprintPath`→`assetPath`；`ownerWidget`→`ownerClass`；`filePath`→`scriptPath`；Lua `path`→`luaPath`；`classPath`→`className` |

旧键一律 `arg_invalid`，无别名兼容。

## 命名

| 动词 | 用途 |
|---|---|
| `get` / `list` / `search` | 只读 |
| `set` | 仅 `*_property` + `updates[]` |
| `interact` | `action` 命令（非 propertyPaths） |
| `manage` | 仅磁盘 `*_asset_*` 结构编辑 |

禁止：`manage_animation`、`set_runtime_actor_animation`。

## 路由（模式 + 例外）

**资产**：首选 `search_asset` → `recommended*`；无推荐时 `{get|manage|create}_asset_{type}` — type ∈ `blueprint` / `material` / `anim_blueprint` / `anim_montage` / `user_widget` / `behavior_tree` / `blackboard` / `data_table` / `data_asset` / `struct` / `texture` / `static_mesh` / `skeletal_mesh` / `anim_sequence` / `skeleton` / `sound_wave` / `sound_cue` / `level` / `level_sequence` / `physical_material`。一 (动词, 类型) 覆盖全部子方面（勿找 `manage_asset_blueprint_variable` 等）。例外：`manage_asset_struct_field`、`get_asset_refs`、`get_asset_lua_binding`、`manage_asset_lua_binding`、`export_asset`、`reimport_asset`、`compile_blueprint`、`save_asset` / `rename_asset` / `duplicate_asset` / `delete_asset` / `unload_asset`。

**运行时**：`{verb}_runtime_{target}[_aspect]`（`list`/`get`/`set`/`spawn`/`destroy`/`interact`/`diff`；target=`actor`/`widget`/`slate_widget`，actor 可加 `_property`/`_animation`/`_behavior_tree`/`_audio`/`_niagara`/`_ai`/`_ability_system`）。动画：读 `get_runtime_actor_animation`，写 `interact_runtime_actor_animation`。非模式：`interact_runtime_widget`、`diff_runtime_actors`、`get_runtime_slate_widget`。

**Lua**：`{eval|dofile|gc|hotreload}_runtime_lua` · `get_runtime_lua_*` · `set_runtime_lua` · `get_asset_lua_binding` · `manage_asset_lua_binding`；`hotreload_runtime_lua` 需 UnLua **2.x**。

**插件门控**：GAS / Niagara / StateTree / MVVM / EQS / MetaSound / PCG / ControlRig / Enhanced Input 仅对应插件+引擎版本才注册；`search_capabilities` `not_found` 即跳过，勿按握手名硬调。Tag 查询 `get_gameplay_tags` 全版本可用。

**编辑器**：`control_pie`、`exec_command`、`search_console_variables`、`capture_viewport`、`get_editor_context`、`get_output_log` / `set_log_capture_filter`、`get_editor_info`。

## 工作流要点

1. 蓝图写前：`get_asset_blueprint(sections=["graphOverview"])`，`graphName` 用返回图名；非 Actor BP 禁 `add_component` / `set_defaults`。BPI：`create_asset_blueprint(parentClass=Interface)`；函数/实现用 `add_function` / `add_interface`。WBP 控件树/动画用 `manage_asset_user_widget`，EventGraph 用 `manage_asset_blueprint`。
2. Lua：先 `get_asset_lua_binding`；`bound=false` 则停止。写绑定用 `manage_asset_lua_binding`。
3. 行为树：改后 `save_asset`；换类型用 `replace_node`；图错位用 `sync_graph`。

## 硬性规则

- 写成功**不**返回 `success:true`，无 `error` 即成功；`interact_*` 改运行时命令态（非 propertyPaths）。
- `*_asset_*` 磁盘；`*_runtime_*` 需 PIE/Game。
- `search_asset` 禁止 `assetType=all` + 裸 `/Game/`；之后用 `recommended*` + `path`。
- `search_capabilities`：先按上述命名模式推名传 `capabilityName`（一次拿全 `parameters[]`），推不出再用窄域 1–2 词 `query`（如 `blueprint graph`）；`query=""` 仅 name 目录；**禁止**单用 `blueprint` / `asset` / `runtime` / `animation`（`query_too_broad`）。失败看 `errorKind` / `suggestedQueries`；`not_found` 且 hint 写未注册 → 勿换词重试同域；`call_capability` 遇 `disabled` 勿重试。
- `get_runtime_actor_property` 必填非空 `actorName`（先 `list_runtime_actors`）；`exec_command` 必填非空 `command`。
- `sections=["all"]` 后 30s 内禁子 section（`redundant_call`）。
- 重试 ≥2 / 无合适 cap / Schema 需猜 / 串行 ≥3 → `submit_feedback`；**`_feedbackHint` 强制**。
