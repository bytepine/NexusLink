NexusLink MCP: Unreal Editor + runtime control (assets / PIE / UMG / Lua / animation / AI / editor).

> Host note: **MCP is Editor / PIE only** (module Type=Runtime; Startup/Shutdown no-op when `!WITH_EDITOR`). Trigger keywords are in the proxy `initializePrefix`.

## Tool model

Meta-tools: `search_capabilities`, `call_capability`, `submit_feedback` (direct `tools/call`). Everything else is a capability via `call_capability(capability, arguments)`; `arguments` must be a nested object.

## First action

When connected to UE: MCP first—do not guess `/Game/...` or grep the repo alone. To read assets: `search_asset` (narrow `assetType` + `pathFilter`) → `assets[].path` + `recommendedGet`/`recommendedManage` (typed at top level; per-item when `all`) → then read/write. Unknown cap → `search_capabilities`. Use business terms only for `search_asset`, not `search_capabilities`.

## Token budget

- Response body is the main cost: keep `sections` narrow (avoid `all`), start with small `limit`; for logs use `get_output_log` ≤50 with `categoryFilter` / `verbosity` / `textFilter`, paginate with `offset` if needed.
- When lists include `<k>_defaults`, missing fields equal that value (`merged={**defaults,**entry}`).
- Diagnostic logs: prefer `preset=diagnose` (newest + ≥warning + `summaryByCategory`/`errorCount` + limit≤50); or manual `order=newest` + `includeSummary`; note `latestSequence` before repro, then `sinceSequence` for deltas. Entries include UTC `time` (ISO-8601).
- **Single target**: capabilities use only `assetPath` / `actorName` / `widgetName`; cross-target batch via `call_capability(calls=[{capability,arguments},…])` in one round. Within one target keep `sections` / `propertyPaths` / `operations` / `updates`.
- Prefer `search_capabilities` with `capabilityName` once for full `parameters[]`.

## Parameter contract (Breaking)

| Canonical | Notes |
|---|---|
| Single target + `calls[]` | No `assetPaths`/`actorNames`/`widgetNames`; cross-target only via meta-tool `calls[]` |
| manage → `operations[]` | No top-level `fields`/`rows`/`keys`/`widgets`/`ops`/bare `action` synthesis |
| get → `propertyPaths[]` | Writes use `updates[].propertyPath` |
| Renames | `newPath`→`destAssetPath`; `blueprintPath`→`assetPath`; `ownerWidget`→`ownerClass`; `filePath`→`scriptPath`; Lua `path`→`luaPath`; `classPath`→`className` |

Legacy keys always → `arg_invalid`; no alias compatibility.

## Naming

| Verb | Use |
|---|---|
| `get` / `list` / `search` | Read-only |
| `set` | Only `*_property` + `updates[]` |
| `interact` | `action` commands (not propertyPaths) |
| `manage` | Disk `*_asset_*` structural edits only |

Forbidden: `manage_animation`, `set_runtime_actor_animation`.

## Routing (patterns + exceptions)

**Assets**: prefer `search_asset` → `recommended*`; otherwise `{get|manage|create}_asset_{type}` — type ∈ `blueprint` / `material` / `anim_blueprint` / `anim_montage` / `user_widget` / `behavior_tree` / `blackboard` / `data_table` / `data_asset` / `struct` / `texture` / `static_mesh` / `skeletal_mesh` / `anim_sequence` / `skeleton` / `sound_wave` / `sound_cue` / `level` / `level_sequence` / `physical_material` / `string_table` / `foliage_type` / `font` / `media_source`. One (verb, type) covers all sub-aspects (do not look for `manage_asset_blueprint_variable`, etc.). Exceptions: `manage_asset_struct_field`, `get_asset_refs`, `get_asset_lua_binding`, `manage_asset_lua_binding`, `export_asset`, `reimport_asset`, `compile_blueprint`, `save_asset` / `rename_asset` / `duplicate_asset` / `delete_asset` / `unload_asset`.

**Runtime**: `{verb}_runtime_{target}[_aspect]` (`list`/`get`/`set`/`spawn`/`destroy`/`interact`/`diff`; target=`actor`/`widget`/`slate_widget`, actor may add `_property`/`_animation`/`_behavior_tree`/`_audio`/`_niagara`/`_ai`/`_ability_system`). Animation: read `get_runtime_actor_animation`, write `interact_runtime_actor_animation`. Non-pattern: `interact_runtime_widget`, `diff_runtime_actors`, `get_runtime_slate_widget`.

**Lua**: `{eval|dofile|gc|hotreload}_runtime_lua` · `get_runtime_lua_*` · `set_runtime_lua` · `get_asset_lua_binding` · `manage_asset_lua_binding`; `hotreload_runtime_lua` requires UnLua **2.x**.

**Plugin-gated**: GAS / Niagara / StateTree / MVVM / EQS / MetaSound / PCG / ControlRig / Enhanced Input / Paper2D / GeometryCollection / CommonUI / Movie Render Queue register only with matching plugin+engine; `search_capabilities` `not_found` → skip, do not hard-call handshake names. Tag queries: `get_gameplay_tags` is always available.

**Editor**: `control_pie`, `exec_command`, `search_console_variables`, `capture_viewport`, `get_editor_context`, `get_output_log` / `set_log_capture_filter`, `get_editor_info`.

## Workflow notes

1. Before blueprint writes: `get_asset_blueprint(sections=["graphOverview"])`, use returned graph names; non-Actor BP forbids `add_component` / `set_defaults`. BPI: `create_asset_blueprint(parentClass=Interface)`; functions/impl via `add_function` / `add_interface`. WBP tree/animation: `manage_asset_user_widget` (`add_track` may take `widgetName`+`propertyPath`); `get_asset_user_widget(sections=["graphOverview"])` lists graphs, EventGraph via `manage_asset_blueprint`.
2. Lua: `get_asset_lua_binding` first; if `bound=false`, stop. Bindings via `manage_asset_lua_binding`.
3. Behavior trees: after edits `saveToDisk` or `save_asset`; change type with `replace_node`; graph drift with `sync_graph`.
4. manage optional `saveToDisk` (default false); only BP/ABP/WBP manage may pass `compile` (default false). Standalone `save_asset` / `compile_blueprint` still available.

## Hard rules

- Write success does **not** return `success:true`; no `error` means success; `interact_*` changes runtime command state (not propertyPaths).
- `*_asset_*` = disk; `*_runtime_*` needs PIE/Game.
- `search_asset`: no `assetType=all` + bare `/Game/`; then use `recommended*` + `path`.
- `search_capabilities`: infer name from patterns above and pass `capabilityName` (full `parameters[]` once); otherwise narrow 1–2 word `query` (e.g. `blueprint graph`); `query=""` is name-only catalog; **do not** use `blueprint` / `asset` / `runtime` / `animation` alone (`query_too_broad`). On failure check `errorKind` / `suggestedQueries`; `not_found` with unregistered hint → do not retry same domain; on `call_capability` `disabled` do not retry.
- `get_runtime_actor_property` requires non-empty `actorName` (call `list_runtime_actors` first); `exec_command` requires non-empty `command`.
- After `sections=["all"]`, no sub-section calls for 30s (`redundant_call`).
- Retry ≥2 / no suitable cap / schema guessing / serial ≥3 → `submit_feedback`; **`_feedbackHint` is mandatory**.
