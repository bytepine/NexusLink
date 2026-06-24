# NexusLink — UE MCP 插件

**当前版本：1.13.1** · 仓库 [bytepine/NexusLink](https://github.com/bytepine/NexusLink)

基于 Unreal Engine 的 MCP 集成插件，将 UE 项目上下文通过 MCP 协议暴露给 AI 工具。

> 支持 UE 4.26 及以上所有版本（含 UE5）

## 架构概览

```
NexusLink 插件
├── FNexusMcpServer ─── HTTP + WebSocket 服务器
│   ├── POST /stream ─── MCP Streamable HTTP（per-session 会话隔离）
│   ├── GET  /status ──── 无状态探测（返回项目信息 + WS 端口）
│   └── WebSocket ──────── 供 Rider / VSCode 代理长连接通信
├── FNexusMcpDispatcher ─── JSON-RPC 分发（SearchMode / MultiTool 双模式）
├── FNexusMcpToolRegistry ── 工具注册表 + REGISTER_MCP_TOOL 宏
├── FNexusMcpTool ────────── 工具基类（3 个元工具）
├── FNexusCapabilityRegistry ── Capability 全局注册表（与 Tool 完全解耦，按名 O(1) 查找）
└── FNexusCapability ─────────── Capability 基类（原子工作单元，独立注册，独立调用）
    └── FNexusMultiSectionCapability ── sections[] 批量分发框架
```

### 暴露模式（ToolsListMode）

| 模式 | tools/list 返回 | initialize.instructions | 适用场景 |
|---|---|---|---|
| **SearchMode**（默认） | 3 个元工具 | `InitializeInstructions.SearchMode.md`（完整路由表） | AI 按需 `search_capabilities` 发现 |
| **MultiTool** | 全部已启用 Capability（各作独立 Tool）+ `submit_feedback` | `InitializeInstructions.MultiTool.md`（精简约束） | 需要 tools/list 一次性枚举所有能力的场景 |

模式切换路径：Editor → Editor Preferences → Plugins → NexusLink → **工具列表模式**。Capability 变更时自动广播 `notifications/tools/list_changed`。

## 安装与启用

从 [NexusLink Releases](https://github.com/bytepine/NexusLink/releases) 下载 `nexus-mcp-unreal-<version>.zip`，或克隆本仓库到项目的 `Plugins/Developer/NexusLink`。

1. 将插件放入项目的 `Plugins/Developer/NexusLink`，在 **Edit → Plugins → Developer → NexusLink** 中启用
2. 重启编辑器后，打开 **Edit → Editor Preferences → Plugins → NexusLink**
3. 勾选 **启用 MCP 服务器**（**默认关闭**）——勾选后即时启动 HTTP（`POST /stream`）与 WebSocket，并注册实例供 Rider/VSCode 发现；取消勾选立即停止，**无需重启编辑器**

未勾选时：工具栏不显示端口、IDE 代理扫描不到实例、AI 直连 `http://127.0.0.1:45000/stream` 无响应。完整用户指南见 [docs/usage-guide.md](docs/usage-guide.md) §2。

### 开发路径

**路径 A — 纯 Tool**（轻量无 section 工具，直接重写 `ExecuteImpl`）
1. 创建 `Private/Tools/<模块>/NexusMcpToolXxx.h/.cpp`，继承 `FNexusMcpTool`
2. 实现 `GetName()` / `GetDescription()` / `ExecuteImpl()`
3. `.cpp` 末尾 `REGISTER_MCP_TOOL(FNexusMcpToolXxx)`

**路径 B — Capability**（主流路径，业务逻辑封装在 Capability，可独立调用）
1. 创建 `Private/Capabilities/<分类>/NexusXxxCapability.h/.cpp`，继承 `FNexusCapability`（多 section 则继承 `FNexusMultiSectionCapability`）
2. 实现 `BuildDefinition()` / `Execute()`；`.cpp` 末尾 `REGISTER_MCP_CAPABILITY(FNexusXxxCapability)`
3. Capability 通过 `call_capability` 元工具直接调用，或在 MultiTool 模式下作为独立 MCP Tool 暴露

---

## 功能列表

### 元工具（Meta）

- [x] `search_capabilities` — 按意图发现 Capability。失败看 `errorKind`（`not_found`/`disabled`/`disabled_only`）；`capabilityName` 精确查询或 `query` 1–2 词；嵌套 `parameters[]`（如 `widgets[].action` enum）
- [x] `call_capability` — 执行 Capability；失败看 `errorKind`（`disabled` 勿重试）。旧名（如 `create_blackboard`）自动映射规范名。**单条** / **批量** `calls[]`
- [x] `submit_feedback` — 上报工具/Capability 使用摩擦，持续改进发现与 schema。触发时机：重试 ≥2 次无进展 / 找不到合适 cap / schema 字段需要猜测 / 被迫 ≥3 串行调用

### 编辑器工具（Editor）

- [x] `get_editor_info` — 获取引擎版本、项目名、平台、构建配置
- [x] `get_editor_context` — 只读编辑器上下文（`sections`：`selection_actors` / `selection_assets` / `content_browser_path`；editor World ≠ PIE）
- [x] `search_console_variables` — 按子串搜索控制台变量名（只读，含当前值）
- [x] `get_output_log` — 查询 UE 输出日志缓冲区（分类/详细程度/文本过滤 + offset/limit 分页，最近 2000 条）
- [x] `set_log_capture_filter` — 动态设置日志捕获白名单（传空数组=捕获全部，传列表=只捕获指定分类）
- [x] `save_asset` — 持久化保存资产（`assetPaths` 批量）；经 `SaveDirtyPackage` 落盘，Live Coding 时返回 `deferred=true`
- [x] `delete_asset` — 删除单个资产包
- [x] `rename_asset` — 重命名/移动资产（引擎自动重定向引用）
- [x] `duplicate_asset` — 复制资产到新路径（任意类型，源资产不变）
- [x] `export_asset` — 将资产导出到磁盘文件（Fbx/Stl 等，依类型）
- [x] `reimport_asset` — 从源文件重新导入资产
- [x] `control_pie` — 控制 PIE（start / stop / status）
- [x] `exec_command` — 执行 UE 控制台命令并捕获输出
- [x] `get_asset_refs` — 查询资产依赖项（dependencies）或引用者（referencers），支持递归/过滤/分页
- [x] `get_gameplay_tags` — 查询 Gameplay Tags 层级树（section=hierarchy）、运行时 Actor 属性（section=actor）、资产属性（section=asset）、按 Tag 查引用资产（section=referencers，需 `tag`）
- [x] `capture_viewport` — 截取编辑器窗口（`editor` / `editor_desktop` 整窗）、面板区域或 PIE 视口（PNG/JPG，支持按 Actor 包围盒裁切、按 UMG Widget 区域裁切、多视角拍摄）；截图存入 `Saved/NexusCaptures/`，自动保留最近 20 张

### 通用资产工具

- [x] `search_asset` — 搜索项目资产（assetType / pathFilter / nameFilter / query，分页）
- [x] `get_asset_refs` — 查询资产依赖/引用（见编辑器工具段）
- [x] `get_asset_lua_binding` — 查询蓝图的 UnLua 绑定（返回 bound/moduleName/filePath；若 `bound=false` 停止，不猜路径）
- [x] `rename_asset` / `duplicate_asset` / `delete_asset` / `compile_blueprint`（`save_asset` 见编辑器工具段）
- [x] `get_asset_texture` — 读取 Texture2D（尺寸、压缩、sRGB、LOD）
- [x] `get_asset_static_mesh` — 读取 StaticMesh（LOD、材质槽、碰撞摘要）
- [x] `get_asset_anim_sequence` — 读取 AnimSequence（时长、帧率、帧数、骨骼引用、notifies 列表）
- [x] `get_asset_skeletal_mesh` — 读取 SkeletalMesh（LOD、材质槽、骨骼、PhysicsAsset）
- [x] `get_asset_skeleton` — 读取 Skeleton（骨骼树分页、Socket 摘要）
- [x] `get_asset_sound_wave` — 读取 SoundWave（时长、采样率、声道）
- [x] `get_asset_sound_cue` — 读取 SoundCue（时长、SoundNode 摘要）
- [x] `get_asset_niagara_system` — 读取 NiagaraSystem（发射器列表；UE5+ 用户参数摘要；需 `WITH_NIAGARA=1`）
- [x] `get_asset_level` — 只读检查关卡（UWorld 包）：`sections` `actors`（分页+过滤）/ `settings`（WorldSettings 摘要）；`editor_only`
- [x] `manage_asset_texture` — 编辑 Texture2D 属性（压缩、sRGB、LODGroup 等）
- [x] `manage_asset_static_mesh` — 编辑 StaticMesh 材质槽与属性
- [x] `manage_asset_skeletal_mesh` — 编辑 SkeletalMesh 材质槽与属性
- [x] `manage_asset_skeleton` — 管理 Skeleton Socket（增删改）
- [x] `manage_asset_anim_sequence` — 编辑 AnimSequence（`add_notify` / `remove_notify` / `set_frame_rate` / `set_root_motion`）
- [x] `manage_asset_sound_wave` — 编辑 SoundWave 属性（`action=set_property`）
- [x] `manage_asset_sound_cue` — 编辑 SoundCue（`set_property` / `add_node` / `remove_node` / `connect_nodes`）
- [x] `manage_asset_niagara_system` — 编辑 Niagara 系统（`set_property` / `set_user_parameter`；`WITH_NIAGARA=1`）
- [x] `manage_asset_level` — 编辑关卡（`set_property` 改 WorldSettings；`spawn_actor` / `remove_actor` / `set_actor_property` 改磁盘 Actor；`editor_only`）

### 蓝图工具（Blueprint）

- [x] `create_asset_blueprint` — 创建新蓝图资产（可指定父类）
- [x] `get_asset_blueprint` — 读取蓝图详情（sections：variables / functions / components / defaults / graphOverview / graphs / all）；自动检测 UnLua 绑定返回 `luaModule` + `luaFilePath`
- [x] `manage_asset_blueprint` — 蓝图全功能批量编辑：变量 CRUD（add/remove_variable）、图表节点操作（add/remove/set_node）、连线操作（connect/disconnect）、SCS 组件（add/remove_component，Actor BP 专用）、CDO 默认值（set_defaults）

### 动画资产工具

- [x] `create_asset_anim_blueprint` — 创建 AnimBlueprint 资产（关联指定 Skeleton，自动编译）
- [x] `get_asset_anim_blueprint` — 读取 ABP 结构（sections：variables / statemachines / defaults / graphOverview）
- [x] `manage_asset_anim_blueprint` — 管理状态机（add/remove state_machine / state / transition）
- [x] `create_asset_anim_montage` — 创建 AnimMontage 资产（关联指定 Skeleton）
- [x] `get_asset_anim_montage` — 读取 AnimMontage（Slot/Segment 列表、Section 列表）
- [x] `manage_asset_anim_montage` — 管理 AnimMontage Segment（add/remove_segment）和 Section（add/remove_section）

### 材质工具（Material）

- [x] `create_asset_material` — 创建 Material 或 MaterialInstance（可指定 materialDomain / parentMaterial）
- [x] `get_asset_material` — 读取材质参数、节点图（sections：parameters / graph / all）
- [x] `manage_asset_material` — 材质批量编辑入口（actions：set_param / add_node / remove_node / set_node / recompile；Texture 节点支持 defaultValue 绑定资产路径并推导 SamplerType）

### 结构体工具（Struct）

- [x] `create_asset_struct` — 创建新 UserDefinedStruct 资产
- [x] `get_asset_struct` — 读取 Struct 字段列表（name/type/defaultValue）
- [x] `manage_asset_struct_field` — Struct 字段批量管理（add/remove/set；支持改名/改类型/改默认值）

### 数据资产工具（DataAsset / DataTable）

- [x] `create_asset_data_asset` — 创建新 DataAsset 资产（需指定父类）
- [x] `create_asset_data_table` — 创建新 DataTable 资产（需指定行结构体类名）
- [x] `get_asset_data_asset` — 读取 DataAsset 属性
- [x] `get_asset_data_table` — 读取 DataTable 行数据（支持 rowNames 过滤、分页）
- [x] `manage_asset_data_asset` — 批量修改 DataAsset 字段（`action`：`set` / `reset`）
- [x] `manage_asset_data_table` — DataTable 行批量增删改（action：add / remove / set）

### 控件蓝图工具（Widget）

- [x] `create_asset_user_widget` — 创建新 WidgetBlueprint 资产（可指定父类）
- [x] `get_asset_user_widget` — 读取 WidgetBlueprint 控件树（`widgets` 含 `layout`）+ UMG 动画列表（`sections=widgets|animations`）
- [x] `manage_asset_user_widget` — 控件树批量管理（`add` / `remove` / `set_slot` / `set_property`；设计时操作）

### Lua 运行时工具（需要 UnLua 插件）

- [x] `eval_runtime_lua` — 执行 Lua 代码片段（需 PIE/Game + UnLua）
- [x] `dofile_runtime_lua` — 执行 Lua 文件（需 PIE/Game + UnLua）
- [x] `set_runtime_lua` — 设置 Lua 全局变量
- [x] `gc_runtime_lua` — 执行 Lua 垃圾回收
- [x] `hotreload_runtime_lua` — 热重载 Lua 模块
- [x] `get_runtime_lua_env` — 读取 Lua 全局环境表概览
- [x] `get_runtime_lua_value` — 按路径读取 Lua 变量值
- [x] `get_runtime_lua_loaded` — 列出已加载的 Lua 模块
- [x] `get_runtime_lua_stack` — 读取 Lua 调用栈
- [x] `get_runtime_lua_metatable` — 检查 Lua 对象元表
- [x] `get_runtime_lua_object` — 读取运行时 Actor 的 Lua 实例数据
- [x] `get_runtime_lua_memory` — Lua 内存统计
- [x] `get_asset_lua_binding` — 查询蓝图的 UnLua 绑定（见通用资产工具）

### 运行时工具（Runtime）— 需要 PIE/Game

- [x] `list_runtime_actors` — 列出当前 World 中的 Actor（classFilter / nameFilter / tagFilter + 分页）
- [x] `spawn_runtime_actor` — 批量生成 Actor（`spawns:[{blueprintPath?|className?,location?,rotation?}]`）
- [x] `destroy_runtime_actor` — 销毁指定 Actor
- [x] `get_runtime_actor_property` — 读取 Actor 属性（sections：components / attach_hierarchy / all；或 diagnose 预设；支持点号路径 + 容器下标；FUNC 调用）
- [x] `set_runtime_actor_property` — 批量写入 Actor 属性（立即生效）
- [x] `diff_runtime_actors` — 对比 Actor 属性差异（两两对比或批量基准模式）
- [x] `get_runtime_actor_animation` — 查询 Actor AnimInstance 运行时状态（sections：state / slots / variables）
- [x] `interact_runtime_actor_animation` — PIE 蒙太奇播放/停止（`action=play_montage|stop_montage|stop_all`）及 AnimInstance 变量写（`set_anim_variable`）
- [x] `get_runtime_actor_behavior_tree` — 读取 Actor 上 AI 的运行时行为树执行状态（section=runtime）
- [x] `interact_runtime_actor_behavior_tree` — 运行时设置黑板键、重启/停止行为树
- [x] `list_runtime_widgets` — 列出运行时所有活跃 UserWidget
- [x] `spawn_runtime_widget` — 在 PIE/Game 中创建 UserWidget 并 AddToViewport
- [x] `destroy_runtime_widget` — 从视口移除并销毁运行时 UMG 面板
- [x] `interact_runtime_widget` — 操作运行时控件（click / check / toggle / set / read；支持 Button/CheckBox/Slider/TextBlock/EditableText/ProgressBar）
- [x] `get_runtime_widget_property` — 读取运行时 Widget 属性；无 `propertyPath` 时含 `layout`
- [x] `set_runtime_widget_property` — 写入运行时 Widget 属性
- [x] `get_runtime_slate_widget` — 通过 Widget Reflector 十六进制地址获取 Slate 控件信息（含 `layout`：锚点、AutoWrapText 等）

### AI 工具（行为树 / Blackboard）

- [x] `create_asset_behavior_tree` — 创建 BehaviorTree 资产（可选同时创建关联 BlackboardData）
- [x] `create_asset_blackboard` — 独立创建空 BlackboardData 资产
- [x] `get_asset_behavior_tree` — 读取行为树节点结构、路径索引与装饰器/服务属性
- [x] `get_asset_blackboard` — 读取 Blackboard Keys
- [x] `manage_asset_behavior_tree` — 管理行为树节点树（`set_root` / `add_node` / `remove_node` / `move_node` / 装饰器与服务 / `set_property`；`add_node` 支持 `childIndex`）
- [x] `manage_asset_blackboard` — 批量增删/重命名 Blackboard Key（支持 bool/float/int/enum/string/name/vector/rotator/object/class）
- [x] `get_runtime_actor_behavior_tree` — 运行时 AI 执行状态（见 Runtime 段）

### GAS 工具（Gameplay Ability System，`WITH_GAS=1` 时注册）

> 需在项目 `.uproject` 中启用 `GameplayAbilities` 插件。Graph 节点编辑仍走 `manage_asset_blueprint`。

**Gameplay Ability**

- [x] `create_asset_gameplay_ability` — 创建 GameplayAbility Blueprint（可指定父类）
- [x] `get_asset_gameplay_ability` — 读取 GA CDO（sections：metadata / tags / costs / graphOverview）
- [x] `manage_asset_gameplay_ability` — 修改 GA CDO：`set_tags`（Tag 容器 + mode）/ `set_policy`（实例化/网络策略）/ `set_cost_cooldown`（Cost/Cooldown GE 绑定）

**Gameplay Effect**

- [x] `create_asset_gameplay_effect` — 创建 GameplayEffect Blueprint
- [x] `get_asset_gameplay_effect` — 读取 GE CDO（sections：policy / modifiers / tags / cues）
- [x] `manage_asset_gameplay_effect` — 批量修改 GE：`set_policy`（Duration/Period）/ `set_tags` / `add_modifier` / `remove_modifier` / `set_modifier`

**AttributeSet**

- [x] `create_asset_attribute_set` — 创建 AttributeSet Blueprint
- [x] `get_asset_attribute_set` — 读取 AttributeSet CDO 中全部 `FGameplayAttributeData` 属性（name / baseValue / currentValue）
- [x] `manage_asset_attribute_set` — 批量 `set`/`reset` AttributeSet CDO 属性默认值

**运行时**

- [x] `get_runtime_actor_ability_system` — PIE 运行时读取 Actor ASC 快照（sections：abilities / effects / attributes；只读）
- [x] `interact_runtime_actor_ability_system` — PIE 施放/取消 Ability、应用/移除 GE、修改 Attribute 基础值

---

## 服务器框架特性

- [x] MCP Streamable HTTP（`POST /stream`），per-session 会话隔离（`Mcp-Session-Id`），多客户端并发安全
- [x] `GET /status` — 无状态探测端点（项目名、引擎版本、WS 端口、`netRole`）
- [x] WebSocket 服务器（默认 55000 起），供 Rider / VSCode 代理长连接；`nexus/instructions` 按 ToolsListMode 返回 `InitializeInstructions.*.md`；`nexus/proxy_config` 返回 `ProxyConfig.json`（连接工具 description、initialize 前缀、错误文案，供代理动态拉取）
- [x] **SearchMode**（默认）：tools/list 仅暴露 3 个元工具，AI 通过 `search_capabilities` 按需发现能力
- [x] **MultiTool**：tools/list 暴露全部已启用 Capability（各作独立 MCP Tool）+ `submit_feedback`；无 `search_capabilities` / `call_capability`
- [x] Capability 变更或模式切换时广播 `notifications/tools/list_changed`
- [x] **启用 MCP 服务器**总开关（默认关闭）：Editor Preferences → Plugins → NexusLink → 服务器；勾选后即时启动 HTTP/WebSocket 并注册实例
- [x] 端口自动分配，冲突时自动切换；实例注册机制支持零扫描发现（`{PID}.json` 写入临时目录）
- [x] **按 Capability 启用/禁用**（`IsCapabilityEnabled`）：Editor Preferences → Plugins → NexusLink → Capabilities；支持分类级 / 单条级勾选
- [x] **全工具响应默认值压缩**（`FNexusResponseCompactorUtils`）：递归扫描对象数组字段，主流值自动抽取为 `<field>_defaults`，降低响应体积；可通过设置面板 `响应默认值压缩` 全局关闭

---

## 相关文档

- [docs/tool-reference.md](docs/tool-reference.md) — Capability 完整参数手册（脚本生成，`py scripts/build_tool_reference.py` 更新）
- [Resources/CapabilitySpec.md](Resources/CapabilitySpec.md) — Capability 元数据规范（命名 / 描述四段式 / 自检清单）
- [Resources/InitializeInstructions.SearchMode.md](Resources/InitializeInstructions.SearchMode.md) — AI 握手时 SearchMode 工作流说明（**First Action** / Tool Model / Intent→Capability 路由 / Hard Rules）
- [Resources/InitializeInstructions.MultiTool.md](Resources/InitializeInstructions.MultiTool.md) — MultiTool 模式精简约束
- [Resources/AIRules.mdc](Resources/AIRules.mdc) — IDE 侧 AI 工作流 Rule 模板（复制到游戏项目 `.cursor/rules/`，见 [usage-guide §2.8](docs/usage-guide.md)）
- [docs/testing.md](docs/testing.md) — pytest E2E 回归测试套件
- [CHANGELOG.md](CHANGELOG.md) — 版本变更记录

## 测试

两层自动化框架：

- **L1 C++ Automation**（`Source/NexusLinkTests/`）：纯工具函数 + 插件加载 + Capability 注册表冒烟 + `FNexusResponseCompactorUtils` 全量断言。需通过 UEEditor-Cmd 手动触发：

  ```bash
  UEEditor-Cmd Nexus.uproject -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi -NoSound -NoSplash
  ```

- **L2 pytest E2E**（宿主推游戏工程 `Tests/`，如 [NexusUnreal](https://github.com/bytepine/NexusUnreal)）：通过 `call_capability` 对所有 Capability 做端到端回归（SearchMode 下调用，不依赖 MultiTool）：

  ```powershell
  pip install -r Tests/requirements.txt
  python Script/run_e2e.py --ue-url http://127.0.0.1:45000/stream
  ```

  报告输出到 `Saved/Logs/TestReport.xml`。详情见 [docs/testing.md](docs/testing.md)。

**新增 Capability 时**：在宿主推游戏工程 `Tests/test_*.py` 对应阶段文件中同步添加至少一个 happy-path 用例，使用 `client.call_capability("cap_name", {...})` 调用形式。

## 本地打包

```bash
py scripts/build_unreal.py --version 1.13.1 --output release/
```

产物：`release/nexus-mcp-unreal-<version>.zip`，解压到 UE 项目 `Plugins/Developer/`。

## License

[MIT](LICENSE) © byteyang

> 新增/修改 Capability 时同步本文件功能列表，并运行 `py scripts/build_tool_reference.py` 重新生成 `docs/tool-reference.md`。
