NexusLink MCP：Unreal 编辑器 + 运行时控制（资产 / PIE / UMG / Lua / 动画 / AI / 编辑器）。

## ⚡ 触发条件（何时使用本工具）

当用户消息包含以下任一关键词时，你**必须使用 MCP 工具**而非凭记忆作答：

`UE` `Unreal` `蓝图` `Blueprint` `资产` `Asset` `Widget` `UMG` `材质` `Material`
`行为树` `BehaviorTree` `动画蓝图` `ABP` `DataAsset` `数据表` `DataTable`
`PIE` `Actor` `GAS` `GameplayAbility` `Niagara` `StateTree` `状态树` `MVVM` `ViewModel` `关卡` `Level` `Lua`
`Montage` `黑板` `Blackboard` `骨骼` `Skeleton` `贴图` `Texture`

## 首要动作（强制）

你已连接 **实时 Unreal 编辑器**。用户问蓝图、Widget、材质、DataAsset、Actor、UI 等任何问题时：

1. **必须先调 MCP** — 禁止凭记忆、猜测 `/Game/...` 路径或仅 grep 本地仓库作答。
2. **读取流程**：`search_asset`（收窄 `assetType` + `pathFilter`）→ `get_asset_*` / `list_runtime_*` → 再分析或编辑。
3. 用户提到资产名且本回合 **尚未调 MCP** → **先调 MCP 再回复**。
4. **IDE 侧可选**：游戏项目将 `Resources/AIRules.mdc` 复制到 `.cursor/rules/`，强化四步流程（见 `docs/usage-guide.md` §2.8）；业务词只用于 `search_asset`，不用于 `search_capabilities`。

| 用户意图 | MCP 调用 |
|---|---|
| 蓝图变量 / 函数 / Graph / 节点 | `get_asset_blueprint` |
| 控件树 / UMG 动画 | `get_asset_user_widget`（`sections`：`widgets` / `animations`） |
| 材质参数 / 节点图 | `get_asset_material` |
| 查找资产路径 | `search_asset` |
| Texture2D / 贴图元数据 | `get_asset_texture` |
| 编辑贴图属性 | `manage_asset_texture` |
| StaticMesh / 静态网格元数据 | `get_asset_static_mesh` |
| 编辑 StaticMesh | `manage_asset_static_mesh` |
| AnimSequence 元数据 | `get_asset_anim_sequence` |
| 编辑 AnimSequence | `manage_asset_anim_sequence` |
| SkeletalMesh 元数据 | `get_asset_skeletal_mesh` |
| 编辑 SkeletalMesh | `manage_asset_skeletal_mesh` |
| Skeleton 骨骼树 | `get_asset_skeleton`（`offset`/`limit` 分页） |
| 编辑 Skeleton Socket | `manage_asset_skeleton` |
| SoundWave / SoundCue | `get_asset_sound_wave` / `get_asset_sound_cue` |
| 编辑 SoundWave / SoundCue | `manage_asset_sound_wave` / `manage_asset_sound_cue` |
| Niagara 系统元数据 | `get_asset_niagara_system`（需引擎启用 Niagara） |
| 编辑 Niagara 系统 | `manage_asset_niagara_system` |
| 关卡布局 / WorldSettings | `get_asset_level`（`sections`：`actors` / `settings`；磁盘关卡，非 PIE） |
| 编辑关卡 WorldSettings 或磁盘 Actor | `manage_asset_level`（`set_property` / `spawn_actor` / `remove_actor` / `set_actor_property`） |
| 导出资产到磁盘文件 | `export_asset` |
| 重新导入外部源文件 | `reimport_asset` |
| 显式编译蓝图 | `compile_blueprint`（可选 `saveToDisk`） |
| 运行时动画状态（PIE） | `get_runtime_actor_animation` |
| 播放/停止蒙太奇（PIE） | `interact_runtime_actor_animation`（`action=play_montage|stop_montage|…`） |
| PIE 技能/GE/属性快照 | `get_runtime_actor_ability_system` |
| PIE 施放技能 / Apply GE / 改属性 | `interact_runtime_actor_ability_system` |
| StateTree 结构（States/Tasks/Transitions/Evaluators） | `get_asset_state_tree`（需引擎启用 StateTree，UE 5.5+） |
| Widget 蓝图 MVVM ViewModel 列表 / Binding 绑定 | `get_asset_view_model`（需引擎启用 MVVM，UE 5.5+） |
| 未知 Capability | `search_capabilities`（直接调 MCP 元工具） |

## 工具模型

3 个 MCP 元工具：`search_capabilities`、`call_capability`、`submit_feedback`。  
其余均为 **Capability**，只能通过 `call_capability` 调用。

## 命名与读写（§6 摘要）

| 动词 | 用途 | 示例 |
|---|---|---|
| `get` / `list` / `search` | 只读 | `get_asset_blueprint`, `search_asset` |
| `set` | **仅** `*_property` + `propertyPaths` | `set_runtime_actor_property` |
| `interact` | 多 `action` 命令（非 propertyPaths） | `interact_runtime_widget`、`interact_runtime_actor_animation` |
| `manage` | **仅** 磁盘 `*_asset_*` 结构编辑 | `manage_asset_blueprint` |

**禁止工具名**：`manage_animation`、`set_runtime_actor_animation`。

## 意图 → Capability 路由

### 资产（磁盘 / 编辑器）— 先推导 cap 名
- **CRUD 模式**：`{get|manage|create}_asset_{type}` — type ∈ `blueprint` / `material` / `anim_blueprint` / `anim_montage` / `user_widget` / `behavior_tree` / `blackboard` / `data_table` / `data_asset` / `struct`
- 一个 (动词, 类型) 覆盖 **全部** 子方面。勿找 `manage_asset_blueprint_variable` 等。
- **例外**：`manage_asset_struct_field`、`search_asset`、`get_asset_refs`、`get_asset_lua_binding`、`save_asset` / `rename_asset` / `duplicate_asset` / `delete_asset`

### 运行时（PIE / Game）— 先推导 cap 名
- **模式**：`{verb}_runtime_{target}[_aspect]` — 动词：`list` / `get` / `set` / `spawn` / `destroy` / `interact` / `diff`
- **目标**：`actor`（+ `_property` / `_animation` / `_behavior_tree`）、`widget`（+ `_property`）、`slate_widget`
- **动画**：读 `get_runtime_actor_animation`；写 `interact_runtime_actor_animation`（`action=play_montage|stop_montage|…`）
- **非模式 cap**：`interact_runtime_widget`、`diff_runtime_actors`、`get_runtime_slate_widget`

### Lua（UnLua + PIE）
- `{eval|dofile|gc|hotreload}_runtime_lua` · `get_runtime_lua_*` · `set_runtime_lua`（全局）· `get_asset_lua_binding`；`hotreload_runtime_lua` 需 **UnLua 2.x**（1.x 返回 error，不执行热重载）

### GAS（`WITH_GAS=1`）
| 用户意图 | Capability |
|---|---|
| GA / GE / AttributeSet 资产配置 | `get/manage/create_asset_gameplay_*` / `get/manage/create_asset_attribute_set` |
| GA Graph 节点 | `manage_asset_blueprint` |
| PIE ASC 快照（只读） | `get_runtime_actor_ability_system` |
| PIE 施放技能 / Apply GE / 改属性 | `interact_runtime_actor_ability_system` |
| Gameplay Tag 字典 / 按 Tag 查引用资产 | `get_gameplay_tags`（`referencers` 需 `tag`） |

### 编辑器 / 杂项（不可推导，枚举）
| 意图 | Capability |
|---|---|
| PIE 启停 | `control_pie`（`action`；`mode`=viewport/simulate） |
| 控制台命令 | `exec_command` |
| 搜索 CVar 名 | `search_console_variables` |
| 截图 | `capture_viewport`（含 `editor_desktop`） |
| 编辑器选中 / Content Browser 路径 | `get_editor_context` |
| Gameplay Tags | `get_gameplay_tags` |
| Output Log | `get_output_log` / `set_log_capture_filter` |
| 引擎/项目信息 | `get_editor_info` |

## 决策规则

1. **读 vs 写**：`get_*` / `list_*` / `search_*` 只读；`manage_*` / `set_*` / `create_*` / `delete_*` 改资产或属性；**`interact_*` 改运行时命令状态**（非 propertyPaths）。
2. **资产 vs 运行时**：`*_asset_*` 操作磁盘；`*_runtime_*` 需 PIE/Game。
3. **优先批量**：`assetPaths[]` / `actorNames[]` / `propertyPaths[]` / `sections[]`。
4. **资产路径**：先 `search_asset`；禁止 `assetType=all` + 裸 `/Game/`。
5. **未知 cap**：`search_capabilities`（元工具）；`capabilityName` 精确名或 `query` 窄域 1–2 词（如 `blueprint graph`）。**禁止**单用 `blueprint` / `asset` / `runtime` / `animation`（`errorKind=query_too_broad`）。失败看 `errorKind`：`not_found` / `disabled` / `disabled_only` / `query_too_broad`（见 `disabledCapabilities[]` / `suggestedQueries[]`），勿与「未注册」混淆。`call_capability` 失败同样看 `errorKind`（`disabled` 禁止重试；旧名如 `create_blackboard` 已自动映射为 `create_asset_blackboard`）。

## 蓝图 / Lua / GAS 工作流

1. **蓝图写入**：先 `get_asset_blueprint(sections=["graphOverview"])` — `graphName` 用返回图名。
2. **非 Actor BP**：`manage_asset_blueprint` 禁止 `add_component` / `set_defaults`（仅 Actor BP）。
3. **Lua**：先 `get_asset_lua_binding`；`bound=false` 则停止。
4. **GAS 资产**：语义字段走 `get/manage_asset_gameplay_*`；Graph 仍走 `manage_asset_blueprint`。
5. **行为树**：`manage_asset_behavior_tree` 改后 `save_asset`；编辑器 Graph 与运行时树同步刷新。

## 硬性规则

- **元工具** — `search_capabilities` / `call_capability` / `submit_feedback` 须直接 `tools/call`。
- `arguments` 必须是 **嵌套对象**。
- **`search_capabilities` 禁止过宽单词** — 勿单用 `blueprint` / `asset` / `runtime` / `animation`；改用 `suggestedQueries` 或 `capabilityName`。
- **`get_runtime_actor_property` 必填非空 `actorName`** — 先 `list_runtime_actors`。
- **`exec_command` 必填非空 `command`**。
- **`search_asset` 必须收窄** — 禁止 `assetType=all` + `/Game/` 无过滤。
- 路径先 `search_asset` 验证；30 秒内 `sections=["all"]` 后禁止子 section（`redundant_call`）。
- 重试 ≥2 / 无合适 cap / Schema 需猜测 / 串行 ≥3 次 → `submit_feedback`。
- **`_feedbackHint`** 出现时必须立即 `submit_feedback`。
