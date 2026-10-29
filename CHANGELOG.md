# Changelog — NexusLink

所有变更记录遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 格式。
版本号遵循 [语义化版本控制](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

## [2.3.0-beta.5] - 2026-10-29

> ⚠️ Pre-release，非生产环境使用。

### Added

- Sequencer / Control Rig 进阶编辑（UE 5.8 官方动画 Toolset）：轨道/分段发现与编辑、不同类型关键帧与通道烘焙、Rig 控件/变换、烘焙到 Control Rig、动画层与动画导出；固定动作映射，原生参数 Schema 按需发现。
- Niagara 深层写入（UE 5.8 官方 Toolset）：系统/发射器属性、渲染器增改删、嵌套动态输入与 Data Interface、User Variables 和 Set Parameters；补齐对应 Schema/数据读回。
- Game Features 会话生命周期（启用插件，UE 5.7+）：发现内置 Feature 插件、查询状态、激活/停用；异步等待引擎回调并读回最终状态，同插件并发保护，不修改描述符或卸载插件。
- 自定义 Toolset 发现（UE 5.8 Registry）：当前启用的 C++ / Blueprint / Python Toolset、分页工具列表、原生输入/输出 Schema 和动画动作映射；遵守 Registry 过滤与禁用规则，注册/注销即时反映，不提供任意 Toolset 执行。
- PCG 参数编辑闭环（UE 5.8 官方 Toolset）：节点设置与读回、图参数定义/移除、实例参数覆盖/重置和子图插入；子图循环及重名预检，仍使用所属异步操作查询。
- PhysicsAsset 精细编辑：从 SkeletalMesh 创建资产，Body 增删、单形状尺寸/局部变换修改与移除、质量缩放和约束角度限制；读回形状索引、几何参数、质量与约束限制。
- GameplayTags 项目配置管理：单标签新增、重命名、删除，Info/INI Sources 读回；使用引擎 INI 编辑器即时落盘，删除检查子标签和资产引用，危险操作不支持批次或事务回滚。
- DataRegistry 只读查询（启用插件，UE 5.0+）：已注册 Registry、Schema、条目 ID、配置/运行时数据源及明确条目的缓存值；未缓存逐项标明，不隐式加载或 Acquire。

### Changed

- 既有 Capability 在 UE 5.8 启用对应官方插件时复用 Registry 同步后端：设置/插件列表、DataRegistry 条目与缓存、受保护的 Tag 写入、PhysicsAsset Body/质量、PCG 删节点/连边、Sequencer 显示帧率、Control Rig Pin、Niagara 发射器删除、MVVM 绑定删除、唯一具名 StaticMesh 材质槽与材质编译。保留同步返回、过滤分页、错误保护及低版本引擎后端；官方禁用/过滤不触发兼容重试。

### Fixed

- 修复 PhysicsAsset 的跨版本兼容：UE4 使用 UnrealEd 提供的生成接口，UE 4.26 通过已有参考骨架字段校验骨骼；UE 5.3 及以下使用旧生成参数签名。
- 修复 UE 5.0–5.4 DataRegistry 来源路径查询的接口兼容，读取已有软引用，不额外加载数据表。
- 修复 UE 4.26 新 GameplayTags INI 来源的创建兼容，使用引擎已有的标签写入接口创建并落盘。

## [2.3.0-beta.4] - 2026-10-28

> ⚠️ Pre-release，非生产环境使用。

### Added

- Slate 进阶操作：悬停、拖拽、下拉框选择、复选框设值、表单填充、文本替换及可见文本等待；表单预检、局部失败位置与同名菜单误点防护。
- UE 5.8 UMG 结构操作：移动与同父重排、包裹、模板/单子项替换、空命名槽填充；读回根、顺序、控件槽及继承槽。
- UE 5.8 官方 Toolset 适配：PCG 实例执行及节点采样、Niagara 深层诊断与明确 Stack Fix、按资产类型约束的语义/相似搜索，以及注册 AgentSkill 的列表与详情；保留官方业务结果、启用过滤及插件前置检查。
- 项目设置只读枚举与明确属性读取，插件信息、依赖/被依赖与资产挂载归属查询。
- 三类截图默认返回标准 MCP PNG/JPEG 图片块，支持仅路径模式、异步取图和批量附件；限制数量/字节并保留省略说明与 FilePath。

- MVVM 完整属性绑定：明确区分 WBP、控件及同类不同名 ViewModel，支持嵌套路径、五种方向、启用/编译标记与可发现的单输入转换函数；读回上下文、纯字段路径和转换引用，支持按 BindingId 删除、编译及保存；无效路径/类型/转换不新增空绑定，保留旧空绑定调用兼容，并修正旧 AddViewModel 未初始化 GUID 导致的编译失败。

### Fixed

- 修复显式 editor 等小写视口目标解析、同秒截图文件覆盖及 MultiTool 图片附件传递；带图片的文本响应在元数据处理后序列化，保留 TTL 等最终字段；已挂接的 Slate 窗口使用原窗口截图，避免离屏重绘触发视口 ensure。
- PCG 图读回补充输入/输出端点与节点路径，连线默认选取真实引脚并校验失败，禁止删除内建端点。

- 修复 UE5 蓝图 float 变量使用旧 Pin 类别而被编译成 int32，恢复浮点默认值及 MVVM 数值绑定的类型一致性；UE4 保留原有 float 类别。

## [2.3.0-beta.3] - 2026-10-27

### Changed

- 统一 Unreal Engine MCP 的中英文首页介绍、插件定位和客户端入口，精简插件展示描述；移除 README 中的 Fab 内容，避免商品页 403 导致链接检查失败。
- 移除 Runtime / Editor 模块的 `PlatformAllowList` 与 `WhitelistPlatforms`，不再在插件描述文件中限定 Win64 / Mac；其他平台仍取决于引擎依赖与源码兼容性。

### Added

- 编辑器 Slate 查询/截图/输入、Automation 发现/执行/停止和非阻塞 Live Coding；UE 5.8 可经白名单复用官方 Toolset，旧版使用引擎兼容实现；长操作按会话查询并支持有界等待、输出契约校验及非事务批次预检。

### Fixed

- 修复 UE 5.2 Blackboard 键 ID 比较的整数转换歧义，恢复跨版本编译。

- `InteractRuntimeWidget` 支持用 `ListRuntimeWidgets` 返回的完整实例 `Path` 作为 `WidgetPath` 精确交互；名称命中多个控件时返回候选路径并拒绝执行，排除已移出视口、已脱离层级、隐藏或处于 Switcher 非活动页的缓存控件，支持当前视口内的嵌套 UserWidget。

## [2.3.0-beta.2] - 2026-10-26

> ⚠️ Pre-release，非生产环境使用。

### Added

- 行为树 / Blackboard `bt_retest`：显式 Pawn/AIController、只读预检、改变条件/等待快照/恢复原值、超时末次证据及领域隔离基线；与 GAS 共用执行和报告代码，保留旧 CLI/导入/报告。Runtime Actor 增加独立 Context，行为树新增路径/运行状态/类型化继承键；SetBlackboard 拒绝错误标量类型。

- 同条件性能报告 `tools/trace_compare`：显式端点导出停录 Trace、离线 HTML/JSON 对比及多次采样中位数；GetTrace 显式时间窗统一 Frames/Timers，完整帧统计新增中位数，CPU 返回总条目数和截断标记。条件不一致、零基线、缺项及分析未完成不会产生虚假的性能变化结论。

- `GetAssetEditPreview`：只读预览蓝图 SetDefaults、DataTable Set、Widget SetProperty 的标量类型、当前值与规范化目标值；对应操作支持 ExpectedValue 和默认关闭的 bVerify，编译后重新定位读回，失败不自动保存，并修正 CDO/Widget 写入前的 Undo 记录时机。

- 通用 GAS `GenericActivation` 模板：JSON 显式端点/Actor 映射、技能及权威事件配置；支持无 PlayerState 的 Actor，ASC 的本地对象身份在每步前重新核对，替换或换图停止；项目自定义失败标签说明只在观察到真实标签时展示。

- 公开 InputSchema 兼容基线与检查器：检测字段删除、类型/枚举收窄和新增必填项，支持按明确的宿主能力集检查；Schema 测试默认导出到宿主 Saved，基线只显式更新。

- GAS 联机诊断与复测：ASC `Context` / `Tags` / `ActivationDiagnostics` / `ActivationEvents`，显式目标的已有技能激活和限时事件捕获；`tools/gas_retest` 以 JSON 用例执行多端步骤、等待与断言，支持只读接入预检、显式端点的用例模板及 HTML / JSON / JUnit 报告，分别展示用例状态、权威端事件与未确认结果；支持离线保存通过基线、对比新增失败/恢复/缺失步骤及各端事件变化，以用例签名核对断言并保留前后完整证据，拒绝覆盖已有基线和对比目录；签名核对供示例工程手动启动的固定用例复测复用，无需 UE 的工具回归纳入 GitHub 云端 CI；失败保留证据、写操作不重试。

### Fixed

- GAS 事件读取返回独立字段快照，避免响应压缩修改捕获缓存、导致第二次读取丢失技能身份；Pawn 的 PlayerState 通过组件提供 ASC 时也可发现。

- MultiSection 显式 section 按注册名称解析，保留大小写兼容并正确展开 `All`，避免 PascalCase 名称被转小写后误报未知 section。
- 恢复实例发现、注册文件、WebSocket 身份验证与代理握手的既有字段拼写，修复已发布 Desktop / Rider / VSCode 无法扫描或连接新版插件；Capability 业务字段仍使用 PascalCase。

## [2.3.0-beta.1] - 2026-10-23

> ⚠️ Pre-release，非生产环境使用。

### Added

- `scripts/audit_cpp_naming.py`：报告类型前缀与文件名、布尔 `b` 前缀、`k` 常量前缀、协议字段 PascalCase（默认不失败）
- UE 4.26+ 编辑器只读 Unreal Insights：`ListTraces` 列出 `Saved/Profiling` 与本机 Trace Store；`GetTrace` 读已停录的 `.utrace`（帧统计、卡顿帧、CPU 计时、Bookmark）。分析未完成时 `bAnalysisComplete=false`，再调一次。UE4 Store 无路径时传 `TraceId` 直接分析流快照，`Path` / `TracePath` 留空，不生成缓存文件；适配低版本类型、CPU 聚合与摘要字段。忽略未结束帧，避免返回非有限耗时或默认时间窗。不尾随正在写入的文件；兼容 UE 5.8 的计时器读取与 Trace Store 智能指针接口。

### Changed

- **破坏性**：Capability 工具 id、入参、出参、动作名和标签改为 PascalCase。布尔字段加 `b` 前缀（`saveToDisk` → `bSaveToDisk`）。不再接受旧的 snake_case / camelCase 名字。MCP 信封（`jsonrpc`、`method`、`params`、`isError`、`structuredContent`）不变。
- 私有 Capability 类名与文件名对齐：`FNexus` + 文件名（去掉扩展名）。例如 `FNexusDestroyRuntimeActorCapability`。
- docs(proxy): 三端会话约定明确 PascalCase、按最终 UE host/port 隔离缓存及快照、非法显式端口在缓存与确认之前返回 `-32602`、切换实例时的缓存边界。
- 参数审计优先检查当前源码，无法抽取时才使用导出的 Schema，避免旧清单掩盖非法参数或污染合成文本测试。

### Fixed

- 恢复 MCP / JSON-RPC 标准字段的大小写，标准客户端可正确解析握手、工具 Schema、annotations 与错误响应；Capability 业务字段仍用 PascalCase。
- SearchMode / MultiTool 统一聚合条目、section、编译和保存错误，错误响应保留结构化详情；`Calls[]` 业务或自动编译失败时尝试回滚内存并跳过自动保存，保存延迟到整批检查通过后执行，明确报告磁盘与运行时副作用的回滚边界。
- `ManageAssetDataTable` 接受非空 `Fields` 动态字典，校验未知字段，`null` 保留默认值；写入记录 Undo，临时行在成功及失败路径均正确析构。
- `RedundantCall` 不再拦截读取，提示与当前数据同时返回；只统计成功读取，写入后清除历史记录。

## [2.2.0] - 2026-10-13

### Added

- feat(mcp): `get_output_log` 会话 `watch`（categories / textIncludes / textExcludes / verbosity）旁路缓冲 1000 条；操作后 `collectWatch=true` 只读这段，不写配置。连续相同日志合并为 `repeat`，溢出返回 `dropped`

### Changed

- UnLua 的 14 个 Capability 与 `FNexusLuaUtils` 移到 [NexusLinkExt](https://github.com/bytepine/NexusLinkExt)（`NexusLinkExt` / `NexusLinkExtEditor`）。本插件不再探测 `WITH_UNLUA`。握手说明只拼已注册片段。工具目录不再收录这些 Capability。UnLua L1 在示例工程 `NexusLinkExtTestSuite`。NexusLinkExt 单独发版（tag `nexus-linkext-v*`）
- 代理写门控 `always` 按当前 MCP 会话记住
- 说明和界面使用「身份验证」「写入前确认」
- MultiTool 的 `tools/list` 在启用集、列表模式和危险 Capability 访问没变时复用上次结果。未超过慢调用阈值的成功 `tools/call` 记 Verbose；出错和慢调用仍按原级别

### Fixed

- `manage_asset_*` 目标资产为空时返回 error
- 响应默认值压缩保留工具已写入的 `*_defaults`。UE 5.8 上这些键按条目转成 `FString`
- `search_asset` 接受 `userdefinedenum` 作为 `UserDefinedEnum` 的类型名

## [2.2.0-beta.2] - 2026-10-12

> ⚠️ Pre-release，非生产环境使用。

### Fixed

- fix(mcp): `manage_asset_*` 在目标资产为空时写入 error 并返回，不再解引用空指针。CapabilitySpec §8.9 要求函数返回的指针在解引用前判空

## [2.2.0-beta.1] - 2026-10-08

> ⚠️ Pre-release，非生产环境使用。

### Added

- feat(mcp): `get_output_log` 会话 `watch`（categories/textIncludes/textExcludes/verbosity）装上旁路缓冲（1000 条），操作后 `collectWatch=true` 只读这段；不写配置。连续相同日志合并为 `repeat`，旁路溢出返回 `dropped`

### Fixed

- fix(mcp): 响应默认值压缩不再剥离工具已写入的 `*_defaults` 键（避免 `get_output_log` 把整页 Error 还原成 Warning）；高基数字段提前放弃；`search_asset` 的 `assetType` 改由自动压缩抽取。UE 5.8 上已有 `*_defaults` 的键按条目转成 `FString`（`Values` 键不再是 `FString`）

### Changed

- refactor(plugin): UnLua 的 14 个 Capability 与 `FNexusLuaUtils` 移到 [NexusLinkExt](https://github.com/bytepine/NexusLinkExt)（`NexusLinkExt` / `NexusLinkExtEditor`）；本插件不再探测 `WITH_UNLUA`。握手说明只拼 `FNexusInstructionRegistry` 里已注册的片段，不扫描其他插件目录。CapabilitySpec §2.1.2 写明扩展插件的模块、危险标签与握手注册。工具目录不再收录这些 Capability。宿主 L1 的 UnLua 用例在示例工程 `NexusLinkExtTestSuite`，不在 `NexusLinkTestSuite`。NexusLinkExt 单独发版（tag `nexus-linkext-v*`）
- docs: 代理写门控 `always` 按当前 MCP 会话记住，不再写成进程级
- docs: 说明和界面里的生造词改为日常说法（鉴权→身份验证，写操作门控→写入前确认）

## [2.1.1] - 2026-09-21

### Added

- feat(mcp): `/status` 增加 `hostKind`（Editor/Game/DedicatedServer，不随 PIE 变）与 `hasPlayWorld`；`get_editor_context` 新 section `current_map`；`control_pie` / `get_editor_info` 回显 `hasPlayWorld`（session 已起但 play World 未就绪时 `state=starting`）；search 推荐与 unknown cap 按宿主过滤（Game/DS 提示改连 Editor）

### Changed

- docs: 中英文落地页与使用指南新增 NexusDesktop 推荐快速路径、Token/写操作安全提示和独立 Game/DS 运行时调试说明；修正 `-server` 进程不读取 Preferences、须显式传 `-EnableNexusMcp`
- feat(mcp): `get_asset_refs` / `exec_python` 补口语 keywords；`search_capabilities` 对原生 C++ `UENUM` 返回越界 hint（UserDefinedEnum 走 `get_asset_enum`）

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
