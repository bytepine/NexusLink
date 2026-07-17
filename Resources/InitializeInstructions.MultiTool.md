NexusLink MCP：Unreal 编辑器 + 运行时控制（**MultiTool 模式**）。

## 工具模型

`tools/list` 已暴露**全部已启用 Capability**，各为独立 MCP Tool（名称即 capability 名）。

- **直接** `tools/call` — **禁止** `call_capability(capability=...)` 包装。
- 本模式**无** `search_capabilities`；以各 Tool 的 `description` / `inputSchema` 为准（含 `[see: ...]`）。
- 元工具：`submit_feedback`。

## 首要动作（强制）

已连接 UE：**先调 MCP** 再答蓝图/Widget/材质/资产问题。流程：`search_asset`（顶层 `assets`；指定类型看顶层 `recommendedGet`）→ 再读写。

## 命名与读写（与 SearchMode 一致）

- **Set** — 仅 `*_property`（如 `set_runtime_actor_property`）。
- **Interact** — `action` 命令（`interact_runtime_widget`、`interact_runtime_actor_animation`）。
- **Manage** — 仅 `*_asset_*` 结构编辑。
- **禁止**：`manage_animation`、`set_runtime_actor_animation`。

## 决策规则

1. **读 vs 写**：只读 `get/list/search`；`manage/set/create/delete` 写资产/属性；**`interact_*` 写运行时命令**。写成功无 `success:true`，无 `error` 即成功。
2. **资产 vs 运行时**：`*_asset_*` 编辑器磁盘；`*_runtime_*` 需 PIE。
3. **Lua**：`hotreload_runtime_lua` 需 UnLua **2.x**（1.x 返回 error）。
4. **批量优先**；**search_asset 必须收窄**；读/写优先用返回的 `recommendedGet` / `recommendedManage`（指定类型在顶层）+ `assets[].path`。
5. **贴图/网格/动画/音频/VFX/关卡 资产 get/manage 成对**（无 `recommended*` 时兜底）：读 `get_asset_texture` / `manage_asset_texture`；`get_asset_static_mesh` / `manage_asset_static_mesh`；`get_asset_skeletal_mesh` / `manage_asset_skeletal_mesh`；`get_asset_anim_sequence` / `manage_asset_anim_sequence`；`get_asset_skeleton` / `manage_asset_skeleton`；`get_asset_sound_wave` / `manage_asset_sound_wave`；`get_asset_sound_cue` / `manage_asset_sound_cue`；`get_asset_niagara_system` / `manage_asset_niagara_system`（Niagara 需插件）；`get_asset_level` / `manage_asset_level`（`editor_only`）。PIE Actor 列表用 `list_runtime_actors`；PIE 动画读 `get_runtime_actor_animation`，写 `interact_runtime_actor_animation`。
6. **GAS PIE**：只读 `get_runtime_actor_ability_system`；施放/Apply/改属性用 `interact_runtime_actor_ability_system`。
7. **编辑器只读**：`get_editor_context`（选中 Actor/资产、Content Browser 路径）；`search_console_variables`；`capture_viewport` 含 `editor_desktop`；Tag 引用资产 `get_gameplay_tags`（`referencers` + `tag`）。

## 蓝图 / Lua / GAS

同 SearchMode：`get_asset_blueprint` 先于图编辑；Lua 先 `get_asset_lua_binding`；GAS Graph 走 `manage_asset_blueprint`；行为树改后 `save_asset`。

## 硬性规则

- 参数符合 `inputSchema`；字符串必填字段不得为空。
- **`get_runtime_actor_property` 必填非空 `actorName`** — 先 `list_runtime_actors`。
- **`exec_command` 必填非空 `command`**。
- `get/manage_asset_*` 前先 `search_asset`；优先用返回的 `recommendedGet` / `recommendedManage`。
- `sections=["all"]` 后 30s 内禁子 section。
- `submit_feedback` 触发条件同 SearchMode；**`_feedbackHint` 强制**。
