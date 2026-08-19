NexusLink MCP: Unreal Editor + runtime control (**MultiTool mode**).

> **Host note**: **MCP runs in the Editor binary** (Type=UncookedOnly; includes `-server`/`-game`; not in cooked Game/Server).

## Tool model

`tools/list` exposes **all enabled capabilities** as separate MCP tools (name = capability name).

- **Direct** `tools/call` — **do not** wrap with `call_capability(capability=...)`.
- This mode has **no** `search_capabilities`; rely on each tool's `description` / `inputSchema` (includes `[see: ...]`).
- Meta-tool: `submit_feedback`.

## First action (required)

When connected to UE: **call MCP first** before answering blueprint/Widget/material/asset questions. Flow: `search_asset` (top-level `assets`; typed `recommendedGet` at top) → then read/write.

## Naming & read/write (same as SearchMode)

- **Set** — only `*_property` (e.g. `set_runtime_actor_property`).
- **Interact** — `action` commands (`interact_runtime_widget`, `interact_runtime_actor_animation`).
- **Manage** — only `*_asset_*` structural edits.
- **Forbidden**: `manage_animation`, `set_runtime_actor_animation`.

## Decision rules

1. **Read vs write**: read-only `get/list/search`; `manage/set/create/delete` write assets/properties; **`interact_*` writes runtime commands**. Write success has no `success:true`; no `error` means success.
2. **Asset vs runtime**: `*_asset_*` = editor disk; `*_runtime_*` needs PIE.
3. **Lua**: `hotreload_runtime_lua` requires UnLua **2.x** (1.x returns error).
4. **Single target**: each tool only `assetPath`/`actorName`/`widgetName`; cross-target = multiple `tools/call` (no `call_capability.calls[]` in this mode). Within one target use `sections`/`propertyPaths`/`operations`/`updates`. **search_asset must be narrowed**; prefer returned `recommendedGet` / `recommendedManage` (typed at top) + `assets[].path`. Lists with `<k>_defaults`: missing fields equal that value (`merged={**defaults,**entry}`).
5. **Texture/mesh/animation/audio/VFX/level asset get/manage pairs** (fallback without `recommended*`): read `get_asset_texture` / `manage_asset_texture`; `get_asset_static_mesh` / `manage_asset_static_mesh`; `get_asset_skeletal_mesh` / `manage_asset_skeletal_mesh`; `get_asset_anim_sequence` / `manage_asset_anim_sequence`; `get_asset_skeleton` / `manage_asset_skeleton`; `get_asset_sound_wave` / `manage_asset_sound_wave`; `get_asset_sound_cue` / `manage_asset_sound_cue`; `get_asset_level` / `manage_asset_level` (`editor_only`); `get_asset_level_sequence` / `manage_asset_level_sequence` / `create_asset_level_sequence`; `get_asset_physical_material` / `manage_asset_physical_material` / `create_asset_physical_material`; `get_asset_string_table` / `manage_asset_string_table` / `create_asset_string_table`; `get_asset_font` / `manage_asset_font` (TTF via `reimport_asset`); `get_asset_foliage_type` / `manage_asset_foliage_type` / `create_asset_foliage_type`; `get_asset_media_source` / `manage_asset_media_source` / `create_asset_media_source`. Gated caps (Niagara / GAS / StateTree / MVVM / IK / PCG / ControlRig / Paper2D / GeometryCollection / CommonUI / MoviePipeline) follow `tools/list`; skip if absent. PIE actors: `list_runtime_actors`; PIE animation read `get_runtime_actor_animation`, write `interact_runtime_actor_animation`; audio/Niagara/AI via `interact_runtime_actor_audio` / `_niagara` / `_ai`.
6. **GAS PIE** (needs GameplayAbilities; skip if absent from `tools/list`): read-only `get_runtime_actor_ability_system`; activate/apply/give/cue/loose tags via `interact_runtime_actor_ability_system`.
7. **Editor read-only**: `get_editor_context` (selection, Content Browser path); `search_console_variables`; `capture_viewport` includes `editor_desktop`; tag referencers via `get_gameplay_tags` (`referencers` + `tag`).

## Parameter contract (Breaking)

Same as SearchMode: no `assetPaths`/`actorNames`/`widgetNames`; manage only `operations[]`; get only `propertyPaths[]`; `newPath`→`destAssetPath`, `blueprintPath`→`assetPath`, `ownerWidget`→`ownerClass`, `filePath`→`scriptPath`, Lua `path`→`luaPath`, `classPath`→`className`. Legacy keys → `arg_invalid`.

## Blueprint / Lua / GAS

Same as SearchMode: `get_asset_blueprint` before graph edits; Lua starts with `get_asset_lua_binding`; GAS graphs via `manage_asset_blueprint`; behavior trees save with `saveToDisk` or `save_asset` (`replace_node` for type changes, `sync_graph` for graph drift). manage optional `saveToDisk`; BP/ABP/WBP may pass `compile`.

## Hard rules

- Arguments must match `inputSchema`; required strings must be non-empty.
- **`get_runtime_actor_property` requires non-empty `actorName`** — call `list_runtime_actors` first.
- **`exec_command` requires non-empty `command`**.
- Before `get/manage_asset_*`, call `search_asset`; prefer returned `recommendedGet` / `recommendedManage`.
- After `sections=["all"]`, no sub-section calls for 30s.
- `submit_feedback` triggers same as SearchMode; **`_feedbackHint` is mandatory**.
