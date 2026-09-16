# Changelog — NexusLink

所有变更记录遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 格式。
版本号遵循 [语义化版本控制](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

### Changed

- docs: 中英文落地页与使用指南新增 NexusDesktop 推荐快速路径、Token/写操作安全提示和独立 Game/DS 运行时调试说明；修正 `-server` 进程不读取 Preferences、须显式传 `-EnableNexusMcp`

## [2.1.0] - 2026-09-15

### Added

- feat(plugin): 双模块——`NexusLink` Runtime（独立 Game/DS 可托管 MCP）+ `NexusLinkEditor`；Shipping 剔除服务器。`-EnableNexusMcp` / `-NexusMcpPort=` / `-NexusWsPort=` / `-NexusAllowLan`
- feat(server): 启停：控制台 > `-EnableNexusMcp` > Preferences（仅编辑器）> 关。`NexusLink.Mcp on|off|status|restart|panel`
- feat(mcp): 危险 cap 三模式（全关 / 每次确认 / 自定义）；`exec_python`（危险）+ `get_python_api`（只读）
- feat(runtime): `get_output_log` / `set_log_capture_filter` / `exec_command` / `capture_viewport` 进 Runtime；编辑器截图走 `capture_editor_panel`
- feat(server): `MaxConcurrentSessions` 默认 16；慢调用 Warning

### Changed

- docs: 38 Runtime + 192 Editor = 230；安装路径 `Plugins/NexusLink`；发版只出 `EngineVersion: 4.26` 通用包
- chore(settings): 危险 cap 默认关闭改为按名记录

### Fixed

- fix(runtime): 未开 PIE 时 runtime 写路径不再落到编辑器关卡 Actor
- fix(mcp): `set_runtime_lua` 的 `value` 接受标量；`dofile_runtime_lua` 缺文件改为条目级错误
- fix(compat): 可选插件 Capability 补齐 UE 4.26–5.8
- fix(asset): 缺失包不再二次加 `/Game/` 前缀崩编辑器；Niagara / CommonUI Style 创建；ViewModel 首次建 View；PCG / ControlRig schema

## [2.1.0-beta.2] - 2026-09-15

> ⚠️ Pre-release，非生产环境使用。

### Added

- feat(runtime): `get_output_log` / `set_log_capture_filter` / `exec_command` / `capture_viewport` 进 Runtime；编辑器截图走 `capture_editor_panel`
- feat(server): 启停：控制台 > `-EnableNexusMcp` > Preferences（仅编辑器）> 关。`NexusLink.Mcp on|off|status|restart`
- feat(ui): `NexusLink.Mcp panel` 游戏内调试面板（PIE / 独立包，会话级启停 Capability）

### Fixed

- fix(runtime): 独立 Game 下 `get_runtime_actor_animation` 状态机空指针

## [2.1.0-beta.1] - 2026-09-14

> ⚠️ Pre-release，非生产环境使用。

### Added

- feat(plugin): 双模块——`NexusLink` Runtime（独立 Game/DS 可托管 MCP）+ `NexusLinkEditor`；Shipping 剔除服务器。`-EnableNexusMcp` / `-NexusMcpPort=` / `-NexusWsPort=` / `-NexusAllowLan`
- feat(mcp): 危险 cap 三模式（全关 / 每次确认 / 自定义）；`exec_python`（危险）+ `get_python_api`（只读）
- feat(server): `MaxConcurrentSessions` 默认 16；慢调用 Warning

### Changed

- docs: 安装路径 `Plugins/NexusLink`；发版只出 `EngineVersion: 4.26` 通用包；L1 测试迁到宿主 `NexusLinkTestSuite`
- chore(settings): 危险 cap 默认关闭改为按名记录

### Fixed

- fix(mcp): `set_runtime_lua` 的 `value` 接受标量；`dofile_runtime_lua` 缺文件改为条目级错误
- fix(compat): 可选插件 Capability 补齐 UE 4.26–5.8
- fix(asset): 缺失包不再二次加 `/Game/` 前缀崩编辑器；Niagara / CommonUI Style 创建；ViewModel 首次建 View；PCG / ControlRig schema

## [2.0.2] - 2026-09-04

### Fixed

- fix(mcp): `search_capabilities` 仅在结果真截断时记 overflow；数组参数补 `items.enum`；检索关键词降噪
- fix(mcp): `get_asset_data_table` 的 `mode=rows` 接受 `rowNames` 或 `nameFilter`；非 Actor Blueprint 可写 `set_defaults`

## [2.0.1] - 2026-09-03

### Fixed

- fix(compat): Mac Clang 链接 `FNexusLanHost::Loopback` 未定义
- fix(plugin): 自定义引擎路径下可选插件（GAS/Niagara 等）探测失败

## [2.0.0] - 2026-08-31

### Added

- feat(mcp): Capability 扩至 227——StringTable / Font / Foliage / Media / Paper2D / GeometryCollection / CommonUI / MoviePipeline / GAS Cue / 多类 create·manage；`saveToDisk`/`compile`；写路径 Undo；`control_movie_pipeline`；局域网绑定
- feat(mcp): 鉴权 token 本机唯一（UE / Desktop / Rider / VSCode 共用）；默认开；额外 Token；复制跨机连接

### Changed

- **BREAKING** refactor(mcp): MCP/AI 可见文案英文化（源码注释 / 设置面板 / 日志仍中文）
- chore(plugin): GAS/Niagara 改为按宿主探测，不再强制启用；发版 zip 排除 L1 Tests
- docs: README 改落地页；安装/接入收口 `docs/usage-guide.md`

### Fixed

- fix(mcp): HTTP `/stream` 与 `/status` 回切 GameThread，避免关编辑器崩溃
- fix(mcp): 设置面板 Capability 按源码目录分组；`relatedCapabilities` 只保留当前宿主已启用项
- fix(compat): UE 4.26–5.8 编译（PIE 暂停、MovieScene、Niagara、promote_pin、StaticMesh、HTTP 热切换等）
- fix(mcp): 额外鉴权 Token 按逗号/分号拆分；WS 停服后弱引用回调；token 落盘失败不再报「已就绪」

### Security

- HTTP/WS 须 Bearer；拒 Origin；body/WS 帧上限 1MB；默认绑 loopback
- 危险 cap 默认禁用；开局域网后同网段持 Token 可连，勿映射公网
- UE 4.26–5.1 的 WS 无法指定绑定地址（绑全部网卡）；关鉴权时启动报 Error
- 鉴权从关切到开时清空已通过的 WS auth 名单；POSIX 上 token 文件权限 0600

## [1.16.2] - 2026-08-12

### Changed

- chore(plugin): 主模块改回 `Type: Runtime`（Game/Server 可链接）；MCP 仅 Editor / PIE 实际运行

## [1.16.1] - 2026-08-12

### Changed

- **BREAKING** feat(schema): 入参按 Schema 严格校验（未知键失败）；仅认 `operations[]`；Capability 仅单目标（跨目标用 `calls[]`）
- **BREAKING** refactor(schema): 参数重命名（不保留旧别名）——`destAssetPath` / `ownerClass` / `scriptPath` / `luaPath` 等；manage 顶层批量统一 `operations[]`

### Added

- feat(mcp): `get_output_log` 增量游标 / `preset=diagnose` / 分类摘要；Warning/Error 始终捕获

### Fixed

- fix(compat): UE 5.8 `FJsonObject` 键类型；定制引擎 `HELP_TEXT`

## [1.16.0] - 2026-08-04

### Added

- feat(host): Runtime 基类 + 宿主过滤；DS/Game 只暴露 Runtime cap；Shipping 不启动 MCP
- feat(server): `NexusLink.EnableMcp 1|0`；`-EnableNexusMcp`
- feat(mcp): `get_asset_refs` 继承方向（children/descendants/parent/ancestors）
- feat(mcp): `manage_asset_behavior_tree` 的 `replace_node` / `sync_graph`；`manage_asset_blueprint` 支持 Event 节点
- feat(feedback): 反馈记录写入插件/引擎/平台版本

### Changed

- chore(plugin): 主模块 `Type` 改为 Runtime（`-server` / Game 可加载；MCP 默认关）

### Fixed

- fix(compat): Montage 时长 API（UE5+ `SetCompositeLength`）
- fix(mcp): `create_asset_blueprint` 确保 BeginPlay；`create_asset_enum` 不再因抽象枚举崩溃；字符串匹配默认不区分大小写
- fix(mcp): `unload_asset` 注册期 ensure

## [1.15.3] - 2026-07-28

### Added

- feat(mcp): 批量读资产自动卸载（高水位 / 包数量）；`call_capability.keepLoaded`；手动 `unload_asset`
- feat(mcp): `search_asset` 结果带 `recommendedGet` / `recommendedManage`

### Changed

- perf(mcp): 单条结果去掉 `results[{}]` 信封；身份字段统一 `path`；成功不再回 `success:true`

### Fixed

- fix(compat): UE 5.2 材质/骨骼网格头文件门槛
- fix(release): CI 开启 Git LFS，发版包不再打成 pointer 文本

## [1.15.2] - 2026-07-17

### Changed

- fix(update-checker): 当前版本改读 `.uplugin` 的 `VersionName`（分发包不含 `VERSION` 文件）

## [1.15.1] - 2026-07-17

### Added

- feat(mcp): SearchMode 下把 cap 名当工具调用时提示改用 `call_capability`
- feat(mcp): WS `nexus/proxy_feedback`，代理层失败进入反馈闭环
- feat(mcp): `get_asset_blueprint` 的 `component` 含自有 / 继承 / C++ 原生组件

### Changed

- feat(mcp): 过宽 `search_capabilities` query 直接拒绝并给建议
- fix(mcp): Schema `required` 字符串拒绝空串；`get_runtime_actor_property` 的 `actorName` 必填

## [1.15.0] - 2026-07-08

### Added

- feat(mcp): BlendSpace / AnimSequence 关键帧与曲线；ControlRig / IKRig / IKRetargeter；MetaSound / MetaSoundPatch；PCG；PoseSearch
- feat(mcp): Enhanced Input、MaterialFunction、MPC、StateTree 写、LevelSequence、PhysicsAsset、EQS、DataLayer
- feat(mcp): `get_asset_behavior_tree` 节点 `flatIndex`；子对象属性可展开

## [1.14.0] - 2026-07-02

### Added

- feat(update-checker): 设置面板检查更新；启动时可静默检查
- feat(feedback): 脱敏参数快照、错误指纹、最小复现草稿、浏览器预填 GitHub Issue
- feat(mcp): `get_asset_state_tree` / `get_asset_view_model`（UE 5.5+）

### Changed

- chore(release): 支持 `X.Y.Z-beta.N` Pre-release；Release 正文仅来自 CHANGELOG
- docs: 英文 README；去掉私有仓引用；商店优先安装说明

### Fixed

- fix(compat): UE 5.8 `FJsonObject` 键类型
- fix(ci): Markdown 页内锚点不再误报 404

## [1.14.0-beta.1] - 2026-06-29

> ⚠️ Pre-release，非生产环境使用。

### Added

- feat(update-checker): 设置面板检查更新；启动时可静默检查
- feat(feedback): 脱敏参数快照、错误指纹、最小复现草稿、浏览器预填 GitHub Issue
- feat(mcp): `get_asset_state_tree` / `get_asset_view_model`（UE 5.5+）

### Changed

- chore(release): 支持 Pre-release；Release 正文仅来自 CHANGELOG
- docs: 英文 README；去掉私有仓引用

### Fixed

- fix(compat): UE 5.8 `FJsonObject` 键类型

## [1.13.1] - 2026-06-24

### Changed

- docs: 插件文档迁入独立仓；新增打包脚本与 GitHub Actions

### Fixed

- fix(mcp): `manage_asset_blueprint` 连线失败不再静默成功；`FindBPPin` 支持友好名

## [1.13.0] - 2026-06-23

### Added

- feat(mcp): BT `move_node` / `childIndex`；动画 notifies；黑板 enum 键；ProgressBar 读写

### Fixed

- fix(mcp): `save_asset` 落盘；HTTP `/stream` 线程安全；检索别名；Mac LiveCoding；视口覆盖层点击穿透

## [1.12.2] - 2026-06-09

### Added

- feat(server): 「启用 MCP 服务器」总开关（默认关），勾选即时启停

### Fixed

- fix(compat): `WITH_EDITOR=0` Game 全版本编译
- fix(plugin): 编辑器退出崩溃；设置面板 Capability 分组路径

## [1.12.1] - 2026-06-04

### Added

- feat(mcp): `nexus/proxy_config`，UE 驱动 IDE 代理的工具说明与 initialize 前缀

## [1.12.0] - 2026-06-04

### Added

- feat(mcp): 编辑器只读与多类 `manage_asset_*`、`export_asset`/`reimport_asset`、运行时 GAS/BT/UMG（91→107）

### Fixed

- fix(mcp): `exec_command` 与 `get_output_log` 打通

## [1.11.0] - 2026-06-03

### Added

- feat(mcp): 只读资产 cap（网格/动画/音频/Niagara/关卡）、`compile_blueprint`、蒙太奇交互；GAS 十件套（`WITH_GAS`）
- feat(mcp): `errorKind`、旧 cap 名映射、UMG/Slate `layout`

## [1.10.0] - 2026-06-01

### Added

- feat(mcp): `duplicate_asset`；`search_capabilities` 精确名短路与 OR 降级

### Fixed

- fix(mcp): WebSocket 收包改 GameThread，慢工具不再掐断 IDE 长连接
- fix(compat): 定制引擎 `WITH_EDITOR_ENCRYPTION`

## [1.9.0] - 2026-05-27

### Added

- feat(mcp): SearchMode / MultiTool 双模式；`call_capability.calls[]`；`get_asset_blueprint` 的 `component` section

### Fixed

- fix(mcp): TTL 元数据双路径注入；设置面板 Capability 树滚动
- fix(compat): UE 5.8 JSON / 引擎 API

## [1.8.1] - 2026-05-12

### Fixed

- fix(mcp): `search_capabilities` 按下划线分词；`diff_runtime_actors` 参数名；反馈报告附原始 `errorText`

## [1.8.0] - 2026-05-11

### Changed

- refactor(mcp): Utils 分层；Capability/Tool 元数据与四段式描述统一；UMG 工具改名

### Fixed

- fix(compat): UE 4.26–5.7 编译
- fix(mcp): `search_capabilities` AND 评分与结果上限

## [1.7.1] - 2026-05-09

### Fixed

- fix(mcp): `search_capabilities` 真 AND + 结果上限；`redundant_call` 提前返回不再重跑

## [1.7.0] - 2026-05-09

### Added

- feat(mcp): 元工具 `search_capabilities` / `call_capability` / `submit_feedback`；反馈落盘 `.nexus-feedback/`
- feat(mcp): `manage_asset_actor`、AnimBlueprint/Montage/BehaviorTree/Blackboard 读写
- feat(server): WebSocket `nexus/instructions`

### Changed

- **BREAKING** refactor(mcp): Capability 与 Tool 解耦，按 cap 名直调；多组 cap 改名（`search_asset`、`get_asset_*` 等）
- **BREAKING** refactor(mcp): 属性读写合并为 `get_property` / `set_property`；批量改为单目标；移除 `get_asset_generic` / `call_blueprint_function` 等
- **BREAKING** refactor(mcp): 输出统一 `results[]`；危险/隐藏工具改枚举可见性

### Fixed

- fix(compat): UE 4.26–5.7 编译
- fix(mcp): 材质/控件/DataTable/Struct 等校验与脏标记

## [1.6.4] - 2026-04-24

### Changed

- feat(mcp): `ToolsListMode` 默认 Starter，省 token

### Fixed

- fix(mcp): `tools/list` 补 `description`

## [1.6.3] - 2026-04-24

### Changed

- fix(mcp): `bMinimalContentText` 默认关——主流客户端不读 `structuredContent`

## [1.6.2] - 2026-04-24

### Added

- feat(mcp): `search_tools` 多词 AND + 别名；`initialize.instructions` 按 `ToolsListMode` 生成

### Changed

- perf(mcp): 响应 JSON condensed；Full 模式不再暴露 `search_tools`

## [1.6.1] - 2026-04-24

### Added

- feat(mcp): `search_tools` + `ToolsListMode`（Full/Starter/Custom）；响应 TTL 元数据；`detail` 分级

### Fixed

- fix(mcp): 日志白名单竞态；蓝图创建立即落盘；HTTP 会话 TTL 清理
- fix(mcp): `search_tools` 在不解析 `structuredContent` 的客户端上仍返回正文
- fix(mcp): 切换 `ToolsListMode` 后广播 `tools/list_changed`

## [1.6.0] - 2026-04-23

### Added

- perf(mcp): 响应默认值压缩（列表重复字段抽到 `*_defaults`）

### Changed

- refactor(mcp): 去掉 `_meta.shape` 尾注（投入产出不划算）；`report_unused_fields` 仍保留

## [1.5.4] - 2026-04-23

### Added

- feat(mcp): `report_unused_fields`；大响应可注入字段形状提示

## [1.5.3] - 2026-04-23

### Added

- feat(mcp): `get_property` actor 的 `section=all`

### Fixed

- fix(plugin): Windows 上反馈「打开目录 / 导出」无响应（相对路径）

## [1.5.2] - 2026-04-22

### Fixed

- fix(mcp): 反馈 `argsDigest` 数组按内容区分，不再按长度误合并
- fix(mcp): 批量工具运行时接受单数参数兜底（schema 仍宣传复数）

## [1.5.1] - 2026-04-21

### Changed

- perf(mcp): Schema 枚举从 description 拆出；token 审计认命名空间前缀与 `InitializeInstructions.md`

### Fixed

- fix(compat): UE 4.27 自动回落 VS2019

## [1.5.0] - 2026-04-21

### Added

- feat(mcp): `get_property` 可调 `Name()` 纯函数；`diff_actors` 批量对比
- feat(mcp): 反馈采集 / `submit_feedback` / `export_feedback`（报告含环境、指纹、Issue 草稿）
- feat(test): L1 Automation + L2 pytest E2E

### Changed

- **BREAKING** refactor(mcp): 六套属性工具合并为 `get_property` / `set_property`
- **BREAKING** feat(mcp): 写入类工具改为 `updates[]` / `operations[]` / 复数数组；成功项省略 `success:true`

### Fixed

- fix(mcp): PIE 中默认拒绝改 SCS；`eval` 自动预置 `UE.*`；属性未命中时区分函数/私有字段

## [1.4.0] - 2026-04-20

### Added

- feat(mcp): `get_asset_property` 容器下标与材质节点穿透；`get_asset` 的 `defaults` 与 `assetPaths[]`
- feat(mcp): 反馈采集 / `submit_feedback` / `export_feedback`
- feat(server): `/status` 返回 `netRole`

### Fixed

- fix(mcp): 材质 Texture 节点绑定；`set_node` 不再静默成功

### Changed

- perf(mcp): 运行时错误消息英文化

## [1.3.1] - 2026-04-17

### Changed

- perf(mcp): Schema 用 enum/default 替代长 description；`initialize.instructions` 大幅缩短
- **BREAKING** refactor(mcp): `list_runtime_widgets` 的 `ownerWidget` → `widgetClass`

### Fixed

- fix(mcp): `get_editor_info` 描述与实际字段对齐

## [1.3.0] - 2026-04-16

### Added

- feat(mcp): `capture_viewport` 支持窗口/面板/`actorName`/`widgetName`/`viewAngle`/`windowIndex`（跨平台 ReadPixels）

## [1.2.0] - 2026-04-16

### Added

- feat(mcp): `create_behavior_tree` / AnimBlueprint / AnimMontage；`exec_command`；`get_asset_refs`；动画/GT/BT/视口截图
- feat(mcp): UnLua 1.x/2.x；`get_lua` / `manage_lua`；工具默认全开

### Fixed

- fix(compat): UE 5.0–5.1 编译
- fix(mcp): `control_pie` status；批量删除重试

## [1.1.1] - 2026-04-15

### Fixed

- fix(mcp): DataAsset 抽象类提示；蓝图节点 K2_ 前缀；材质断开 Material 输入；CheckBox `isChecked`；MI 自动推断

## [1.1.0] - 2026-04-15

### Added

- feat(mcp): Lua 求值/调用栈；`diff_actors`；Slate；材质工具集；属性批量/诊断；工具启用面板（默认只读）

### Changed

- refactor(mcp): 11 个资产工具合并为 `get_asset` / `get_asset_property` / `set_asset_property`（49→41）

### Fixed

- fix(compat): UE 5.3–5.7 材质 API；端口冲突顺延；工具列表刷新；运行时动态子控件

## [1.0.1] - 2026-04-14

### Changed

- refactor(mcp): `list_data_assets` 并入 `list_assets`；行操作合并为 `manage_data_table_row`

### Fixed

- fix(compat): UE 4.26–5.7 编译（含 5.2 C4668、缺 include）

## [1.0.0] - 2026-04-13

### Added

- feat(mcp): MCP 服务器（HTTP Streamable + WS + `/status`）；实例注册；端口冲突切换；47 个工具（蓝图 / DataTable / DataAsset / Struct / Widget / Actor / PIE）
