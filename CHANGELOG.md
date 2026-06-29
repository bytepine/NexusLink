# Changelog — NexusLink

所有变更记录遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 格式。
版本号遵循 [语义化版本控制](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

## [1.14.0-beta.1] - 2026-06-29

> ⚠️ Pre-release，非生产环境使用。

### Docs

- docs: README 移除顶部「仓库：bytepine/NexusLink」行（中英文）
- docs: README「与 IDE 代理配合使用」补充商店优先安装——JetBrains Marketplace / Open VSX / VS Marketplace 直链，Releases 作备用

### Chore

- chore(release): 支持 Pre-release 发版流程——版本号 `X.Y.Z-beta.N` 触发 GitHub pre-release；`extract_release_notes.py` 支持 pre-release 版本校验

### Fixed

- fix(compat): `BuildRedactedArgsSnapshot` 使用 `FString(*Pair.Key)` 替换直接赋值，修复 UE 5.8 中 `FJsonObject::Values` Key 类型由 `FString` 变更为 `UE::FSharedString` 导致的 C2664 编译错误；全版本（UE 4.26–5.8）PASS=10
- fix(ci): link-check 忽略 Markdown 页内锚点（`#...`）；`markdown-link-check` 无法校验中文标题 fragment，会误报 404

### Docs

- docs: README 移除「当前版本」行与本地打包示例中的具体版本号（改为 `<version>` 占位），避免随发版漂移；保留 UE 4.26+ 兼容声明

### Chore

- chore: 更新插件图标 `Resources/Icon128.png`（128×128），与 Rider / VSCode 代理统一为六边形 + 连接枢纽品牌视觉

### Added

- feat(update-checker): 新增插件版本检查功能——`FNexusUpdateChecker`（`GetCurrentVersion` / `IsNewerVersion` / `CheckAsync`）；版本来源走 GitHub 官方 `releases/latest` 重定向端点（非 REST API，无速率限制、无需 Auth），自动跟随 302 后从 Release 落地页 `og:url`/`canonical` 解析最新 tag，全 UE 版本统一路径（不依赖 5.4+ 的 `GetEffectiveURL`）；编辑器设置面板新增「插件信息」分类，显示当前版本号 + 「检查更新」按钮（点击先弹「正在检查…」过渡态，结果就地更新为最新/已是最新/失败三态），并新增「启动时自动检查更新」开关（`bCheckUpdateOnStartup`，默认开启）；模块启动时按开关后台静默检查一次，有新版本时弹出含 Releases 页面超链接的通知；失败原因仅写入 `LogNexusUpdateChecker` 日志，通知只提示「检查更新失败，请检查网络连接」；`ParseLatestTagFromReleasePage` 提升为公开 API；`NexusLink.UpdateChecker.IsNewerVersion` / `ParseReleasePage` / `GetCurrentVersion` 三组 AutomationTest 覆盖 semver 比较、HTML 解析（og:url/canonical/fallback/无前导 v/空页）与 VERSION 读取冒烟

- feat(feedback): 报告/环境/指纹增强（P2）——`BuildEnvironmentBlock` 补充 `ToolsListMode`（SearchMode/MultiTool）、反馈条数与时间窗（最早 ts ~ 最晚 ts）；Markdown 报告新增 §8「错误指纹 Top 5（去重聚合）」（`NormalizeForThrottle(errorText)` 做指纹归并，表格含指纹/次数/代表样本）；Issue 草稿新增「搜索无命中 Top Query」段（`search_zero` 条目 Top 5 query 汇总）；测试补齐：报告含 §8 段、环境含 `ToolsListMode`

- feat(feedback): ArgsDigest 改脱敏参数快照——`call_arg_invalid`/`call_fatal` 两类自动埋点的 `ArgsDigest` 字段从「参数 key 名列表」改为脱敏 condensed JSON 摘要：敏感 key（`password/token/secret/apikey/api_key`，大小写不敏感）值替换为 `"<redacted>"`，整体截断 200 字符；`FNexusFeedback::BuildRedactedArgsSnapshot` 提升为公开 API（可测试）；`NexusLink.Feedback.RedactedArgsSnapshot` 自动化测试覆盖：普通 key 保留、敏感 key 脱敏、超长截断、空参数四档

- feat(feedback): Issue 草稿排障增强——`BuildIssueDraftFromRecords` 新增「## 最小复现（自动选取）」块（按权重 `call_arg_invalid×30 > call_fatal×30 > misuse×20 > …` 选 Top1 记录，输出 `ts/tool/capability/errorText/argsDigest/attemptedArgs/actualError/expectedField/query`）；「AI 上报」段改为结构化字段优先（`attemptedArgs/actualError/expectedField` 各成行，`note` 作补充）；Issue 标题改为 `[NexusLink] <capability>: <规则化错误>` 而非分类计数；`IssuePrefill` 自动化测试补齐断言（标题含 capability、正文含「最小复现」/`attemptedArgs`/`expectedField`/AI 上报段）

- feat(feedback): 浏览器预填 GitHub Issue——`UNexusLinkSettings` 新增可配置 `FeedbackIssueRepo`（`owner/repo` 或 GitHub URL，默认 `bytepine/NexusLink`）；设置面板 **创建 GitHub Issue** 按钮读取当前 `feedback.jsonl` 生成标题/正文并在浏览器打开 `issues/new?title=&body=`（不清空数据）；`ExportReport` 报告末尾新增 §8 预填草稿与链接；`FNexusFeedback::BuildIssueDraft` / `BuildIssuePrefillUrl` / `OpenIssuePrefillInBrowser` 公开 API

- feat(statetree): 新增 `get_asset_state_tree` 只读能力（UE 5.5+）：检查 `UStateTree` 资产的 Schema、SubTrees 状态树（递归 States/Tasks/EnterConditions/Transitions/Children）、Evaluators、GlobalTasks（5.5+）、参数数量
- feat(mvvm): 新增 `get_asset_view_model` 只读能力（UE 5.5+）：从 Widget 蓝图 MVVM 扩展读取 ViewModel 列表（类/创建方式/名称）及 Binding 快照（SourcePath↔DestinationPath/BindingType/enabled/compile）
- feat(build): `NexusLink.Build.cs` 新增 `WITH_STATETREE` / `WITH_MVVM` 可选模块探测（仿 GAS 模式）：`StateTree.uplugin` / `ModelViewViewModel.uplugin` 存在且引擎 ≥ 5.5 时自动链接；环境变量 `WITH_STATETREE` / `WITH_MVVM` 可强制开关；UE 4.26 / 5.0–5.4 编译为空
- feat(search): `search_asset` 新增 `StateTree` / `st` 资产类型快捷词；`mvvm` / `viewmodel` 归一到 `widget` 搜索
- feat(compat): `NexusVersionCompat.h` 新增 `NX_UE_HAS_STATETREE_GLOBAL_TASKS`（5.5+）和 `NX_UE_HAS_MVVM_EVENTS_CONDITIONS`（5.5+）语义宏
- docs: `CapabilitySpec.md` §6.4 登记 `get_asset_state_tree` / `get_asset_view_model`；`InitializeInstructions.SearchMode.md` 补路由表行与触发关键词
- test: 新增 `StateTreeMvvmCapabilityTests.cpp`：元数据/注册表/参数校验/无效路径四类 UE Automation 测试，覆盖 `#if WITH_STATETREE` / `#if WITH_MVVM` 门控与 `search_asset` 类型归一逻辑

- chore(unreal): `NexusLink.uplugin` 声明 `GameplayAbilities` / `Niagara` 插件依赖，与 `NexusLink.Build.cs` 可选模块链接一致

### Docs

- docs: README 补充发版流程（与 NexusRider / NexusVSCode 一致）
- docs(testing): 宿主 E2E 策略——默认 headless、不可覆盖打标走 GUI、全量 `--gui`/`--full`

### Chore

- chore(ci): Release 强制 `--verify` 门禁，正文仅来源于 CHANGELOG 段落（禁手写 Release 说明）

### Fixed

- fix(search_capabilities): `UNexusLinkSettings` 空指针统一防护，消除 ForwardNull 静态分析告警

### Chore

- chore(ci): 新增 tag 触发 Release workflow，CI 自动打包并发布 GitHub Release

### Docs

- docs: 架构与流程图改用 Mermaid（README、architecture、usage-guide）
- docs: 移除对私有仓 NexusUnreal / NexusWork 的引用
- docs: 新增英文 README（README.en.md）与中英文顶部语言切换

## [1.13.1] - 2026-06-24

### Changed

- docs: 插件文档迁入独立仓（README、docs/、CHANGELOG、VERSION）
- chore: 新增 `scripts/build_unreal.py` 与 GitHub Actions 打包工作流

### Fixed

- `manage_asset_blueprint` connect 修复 `TryCreateConnection` 失败静默成功；`FindBPPin` 支持 `pinFriendlyName` 匹配；引脚未找到时列出可用 pinName；补 `manage_blueprint_wires/graph/component` 旧名映射

### Docs

- 同步 v1.13.0 能力与 `tool-reference`（notifies、ProgressBar、move_node、search 别名、save deferred）；补 E2E 回归用例
- 全量文档对齐 — README 功能描述补全、ZH_DESCRIPTIONS/ZH_WHEN_TO_USE 覆写补全、SearchMode 路由修正；新增 `audit_doc_sync.py` 门禁脚本

## [1.13.0] - 2026-06-23

- 扩展行为树/动画/黑板/UMG 能力：`manage_asset_behavior_tree` 新增 `move_node` 与 `childIndex` 插入；`get_asset_behavior_tree` 输出路径索引与装饰器属性；`get_asset_anim_sequence` 输出 notifies；`manage_asset_blackboard` 支持 enum 键；`interact_runtime_widget` ProgressBar 读写
- 修复 `save_asset` 落盘与 HTTP `/stream` 线程安全；`search_asset`/`search_capabilities` 检索别名与路由提示；Mac Editor LiveCoding 依赖；视口端口覆盖层点击穿透
- 文档同步 get/manage 读写边界、9 类资产发现链与 `AIRules.mdc` IDE 工作流模板

## [1.12.2] - 2026-06-09

- 新增「启用 MCP 服务器」总开关（默认关闭），勾选后即时启停 HTTP/WebSocket 与实例注册
- 系统性修复 `WITH_EDITOR=0` Runtime/Game 全版本编译兼容；注册期 ensure 降级改由 CI audit 门禁
- 修复编辑器退出崩溃与设置面板 Capability 分组路径解析

## [1.12.1] - 2026-06-04

- 新增 `nexus/proxy_config` 与 `ProxyConfig.json`：UE 驱动 IDE 代理的连接工具 description、initialize 前缀与错误文案，扩 Capability 时无需改 Rider/VSCode
- `ProxyConfig.initializePrefix` 前置 MCP 触发规则；优化元工具四段式 description 与 `InitializeInstructions` 触发引导，促进 AI 主动调用 MCP

## [1.12.0] - 2026-06-04

- Epic 差距补全：新增/扩展编辑器只读（`get_editor_context`、`search_console_variables`、`capture_viewport` editor_desktop、`get_gameplay_tags` referencers）及多类 `manage_asset_*`、`export_asset`/`reimport_asset`、运行时 GAS/行为树/UMG 销毁等 Capability（91→107）
- 修复 `exec_command` 与 `get_output_log` 打通（LogConsole 镜像 + GLog 增量合并）；WebSocket 连断日志降为 Verbose，减少 Output Log 刷屏
- 文档同步 107 Capability 计数、usage-guide 与 MultiTool 路由说明

## [1.11.0] - 2026-06-03

- 新增多类只读资产 Capability（贴图/静态网格/骨骼/动画序列/音频/Niagara/关卡）、`compile_blueprint` 与 PIE 蒙太奇交互 `interact_runtime_actor_animation`；`WITH_GAS=1` 时 GAS 十件套
- MCP 体验：`search_capabilities`/`call_capability` 的 `errorKind`、旧 Capability 名自动映射、嵌套 `parameters[]`（含 `widgets[].action` enum）；UMG/Slate `layout` 导出（`FNexusWidgetLayoutUtils`）
- 工程化：Capability 命名规范与 audit 门禁、跨版本语义宏与 UE4.26–5.8 零警告全量编译、`tool-reference` 脚本化（91 cap）与 `FNexusAssetUtils` 集中访问

## [1.10.0] - 2026-06-01

- 新增 `duplicate_asset` 资产复制能力；`search_capabilities` 支持精确 cap 名短路及 AND 零命中时 OR 降级 Top3
- WebSocket 收包改 GameThread 异步派发，修复慢工具阻塞 Tick 导致 IDE 长连接断开；定制引擎 `WITH_EDITOR_ENCRYPTION` 编译兜底（含 Rider 直编 C1189）
- 反馈驱动优化若干 capability 搜索关键词与错误提示（蓝图/Lua 参数缺失、无绑定结构化返回等）；节流正则静态化与头文件依赖精简

## [1.9.0] - 2026-05-27

- 新增 SearchMode / MultiTool 双模式切换（编辑器偏好 → 服务器 → 工具列表模式）：MultiTool 模式下每个已启用 Capability 独立暴露为 MCP Tool，`tools/list` description 末尾自动追加 `[requires: ...]` / `[see: ...]`，并加载专属精简版 initialize.instructions；模式切换或 Capability 变更时广播 `notifications/tools/list_changed`
- 修复 TTL 元数据注入（MultiTool / SearchMode 双路径均正确注入 `_ttl_seconds`）与 `slow_call` 反馈 `F.Tool` 字段；`call_capability` 新增 `calls[]` 批量顺序调用，子项失败隔离，与 `capability` 互斥；`get_asset_blueprint` 新增 `component` section，输出 SCS 扁平组件列表与 SceneComponent 层级树
- 兼容 UE 5.8 API 变更（`FJsonObject::Values` 键类型、`GetObjectsWithOuter` flags 枚举、`OnPostEngineInit` 访问器、`Engine/UserDefinedStruct.h` 路径移除）；修复设置面板 Capability 树滚动条计算错乱

## [1.8.1] - 2026-05-12

- 修复 `search_capabilities` 搜索质量：`BuildKeywords` 对 Name 额外按下划线分词入索引，覆盖 `"actor property"`、`"search asset"`、`"console command"` 等 search_zero 场景；runtime actor property cap 补 `character` 关键词，覆盖 character 相关搜索
- 修复 `diff_runtime_actors` 描述补 `actorNameA/actorNameB` 参数名，减少 AI 误传参数
- 修复导出反馈报告在 §4 误用区追加原始 `errorText` 样本，便于直接排查真实失败原因

## [1.8.0] - 2026-05-11

- Utils 模块化重构：新增 NexusJsonUtils / NexusBlueprintGraphUtils / NexusPropertyReportUtils / NexusEditorCaptureUtils / NexusCapabilityIndexUtils / NexusCapabilityResultBuilder 六个 Util 工具类，建立七层分层架构（Common/Result/Reflection/Asset/Runtime/Domain/Editor），消除约 1000+ 行重复样板代码（P1 lambda 外壳、P2 资产 finalize、P3 RequirePlayWorld、P4 分页、P5 entry 错误）；新增 CapabilitySpec.md §7 Utils 模块规范；统一 Utils 命名约束，重命名 FNexusStringMatchUtils / FNexusResponseCompactorUtils
- Capability & Tool 元数据框架统一：BuildDefinition 合并全部元数据虚函数钩子（Capability 8 个 + Tool 4 个）；建立四段式描述强规范与注册期自动校验；capability 命名全面对齐 pattern（UMG 三工具改名、manage_asset_actor 合并入 manage_asset_blueprint）；重写 InitializeInstructions.md 路由指南
- 修复跨 UE 版本编译兼容（UE4.26–5.7 全版本 PASS=9），修复 search_capabilities AND 逻辑评分及 MaxSearchResults 上限

## [1.7.1] - 2026-05-09

- 修复 `search_capabilities` 评分改为真 AND 逻辑并新增 `MaxSearchResults` 上限配置（默认 8），溢出时返回 hint 引导精确查询
- 修复 `redundant_call` 命中时提前返回 soft warning，不再执行重复的子 section 查询

## [1.7.0] - 2026-05-09

- feat(unreal): 完善 Capability 反馈采集子系统——新增 3 个自动埋点 category（`call_arg_invalid`：required 字段缺失时与 `call_fatal` 平级记录；`redundant_call`：同 capability+identity 窗口内发过 `sections=["all"]` 后再发子 section；`slow_call`：执行耗时超过 `SlowCallThresholdMs` 阈值）；`BuildThrottleKey` 改为规则化（数字/路径/引号字符串替换为 `*`，防止同类错误穿透节流）；`BuildMarkdown` §4 改为 capability×错误前缀二级分组，新增 §6 按 MCP Tool 统计、§0 趋势对比（与上期 `archive/last_summary.json` diff）；`ExportReport` 同步输出 `report_<ts>.json`（含 `categories`/`tools`/`totalCount`），归档时写 `last_summary.json` 并按 `MaxArchiveCount` 清理旧文件；`submit_feedback` schema 新增 `attemptedArgs`/`actualError`/`expectedField` 结构化字段（`note` 改为可选）；`UNexusLinkSettings` 新增 `SlowCallThresholdMs`/`RedundantCallWindowSec`/`MaxArchiveCount` 配置项；新增 3 个自动化测试（`NexusLink.Feedback.RoundTrip`/`Throttle`/`RuleNormalize`）

- refactor(unreal): 删除 `ENexusToolVisibility` 枚举与 `FNexusMcpToolDefinition::Visibility` / `FNexusMcpTool::GetVisibility()` 钩子（`ForceDisabled` 无任何工具实际使用、`Hidden` 标了 3 个元工具但从未被任何过滤路径检查，全是死代码）；同步移除 `FNexusMcpToolRegistry::GetEnabledDefinitions()` / `FindDefinition()`、`HandleToolsList` 改用 `GetAllDefinitions()`、`HandleToolsCall` 移除恒不命中的 ForceDisabled 拒绝分支；`submit_feedback` / `search_capabilities` / `call_capability` 三个 .h/.cpp 移除空操作的 `GetVisibility()` override

- feat(unreal): WebSocket 通道（`DispatchDirect`）新增 `nexus/instructions` method，返回 `InitializeInstructions.md` 内容；供 vscode/rider 代理连上后异步拉取并拼接到自身 `initialize.instructions`，让 AI 能看到 UE 端定义的 capability 工作流说明（修复此前 WS 路径下 `BuildInitializeInstructions()` 永不触发、md 文件等于死代码的问题）

- docs(unreal): 重写 3 个元工具 description 与 InitializeInstructions.md，纠正 capability 与 tool 的混淆（Workflows 改为 call_capability 显式形式），新增 Tool Model 段与 Feedback 段，引导 AI 正确组合调用链

- feat(unreal): 重建 Capability 反馈子系统——`FNexusFeedback` 静态 API（`Public/Feedback/NexusFeedback.h`）落盘 `.nexus-feedback/feedback.jsonl`；`search_capabilities` 自动埋点 `search_zero`（无命中）/ `search_overflow`（命中超阈值）；`call_capability` 自动埋点 `call_unknown` / `call_disabled` / `call_fatal`（含 30 秒同键节流）；新增 `submit_feedback` 元工具（`FNexusSubmitFeedbackCapability`）供 AI 主动上报 `wrong_tool` / `misuse` / `schema_guess` 等痛点；`ExportReport()` 聚合生成 Markdown 报告（§1 概览 / §2 搜索失败 / §3 搜索过载 / §4 Capability 误用 / §5 工具选错）并归档 JSONL；设置面板新增「AI 反馈」分类（统计行 + 打开目录 / 导出报告 / 清空三按钮）；`UNexusLinkSettings` 新增 `bEnableFeedback` / `SearchOverflowThreshold` 两项可配置字段。UE_4.26 编译 PASS=1, FAIL=0, WARNINGS=0

- fix(unreal): 跨版本编译兼容修复（全 9 版本 FAIL=0 WARNINGS=0）
  - `NexusSearchAssetCapability`/`NexusDeleteAssetCapability`：显式 include `NexusPropertyUtils.h`，修复 UBT Unity Build 不稳定导致的 `NEXUS_FILTER_ADD_CLASS`/`NEXUS_ASSET_CLASS_NAME` 宏缺失（5.4+）；`TrimStartAndEnd` → `TrimStartAndEndInline`（nodiscard，4.27+）
  - `NexusCreateAssetMaterialCapability`：`MD_Surface` 等枚举改用 `EMaterialDomain::` 限定形式；5.3+ 加 `#include "MaterialDomain.h"` 获取完整定义
  - `NexusManageAssetBlueprintCapability`：修正 `NX_UE_HAS_CPF_BLUEPRINT_READWRITE` 宏（原 `AT_LEAST(5,0)` 错误，改为 `0`，CPF_BlueprintReadWrite 在 5.0 已移除）
  - `NexusManageAssetBehaviorTreeCapability`：加 `#include "BehaviorTree/BlackboardData.h"`；`TArray<UBTDecorator*>&` → `auto&` 适配 `TObjectPtr`（5.4+）
  - `NexusGetAssetAnimMontageCapability`/`NexusManageAssetAnimMontageCapability`：`FAnimSegment::AnimReference` 在 5.1 deprecated、5.6 变 protected；新增宏 `NX_UE_HAS_ANIM_SEGMENT_ACCESSOR(5.1+)` 切换 `Get/SetAnimReference()`
  - `NexusManageAssetAnimBlueprintCapability`：修正 `FString::Printf` 参数多余的 `*` 解引用（5.6+ consteval 检查触发 C7595）
  - `NexusManageAssetMaterialCapability`：`FindObject(ANY_PACKAGE)` 用 `NX_UE_HAS_FIND_FIRST_OBJECT` 守护，修复 5.7 ANY_PACKAGE 彻底移除的 error

- feat(unreal): 新增 `manage_asset_actor` 工具（`Capabilities/Asset/Actor/`）——Actor Blueprint 组件树管理与 CDO 默认值写入；actions: `add_component` / `remove_component` / `set_component_property` / `set_defaults`；属性路径支持 "A.B.C" 点分嵌套；职责与 `manage_asset_blueprint`（变量/节点/连线）完全不重叠

- fix(unreal): `manage_asset_widget` 补 `Arguments.IsValid()`；拒绝空 `widgets`；引入 `bDidMutate`，末尾单次 `MarkPackageDirty`；`parentWidget` 找不到时报错并回滚新建控件（不再静默退化为 root）；`hint` 改为顶层字段且仅在有变更时输出；修正缩进；`BuildTags` 前移

- fix(unreal): `get_asset_widget_tree` 找不到 WBP/WidgetTree 改为 fatalError；`assetPath` 补空字符串校验；`name` 改为包路径 `path`；`BuildTags` 前移；改用 `TryGetStringField`

- fix(unreal): `create_asset_widget` 增加 `Arguments.IsValid()`/空路径/`IsValidLongPackageName`/`DoesPackageExist` 校验；存在检测从 `LoadObject` 改为 `DoesPackageExist`；`path` 改为包路径；非 UserWidget 子类/父类不存在改 fatalError；`BuildTags` 前移；补 `#include "Misc/PackageName.h"`

- fix(unreal): `get_gameplay_tags` 修复 `CapCollectTagChildren` 节点双重计数 bug（`SubCount = Count + 1` → `SubCount = Count`），实际输出由约 limit/2 恢复为正确的 limit 个节点

- fix(unreal): `manage_asset_struct_field` 校验 `Arguments` 有效性；拒绝空 `fields`；`action`/`fieldName` 为空时不再误触发 compile/dirty；引入 `bItemMutated` 标志，仅真实变更后调用 `CompileStructure`，末尾单次 `MarkPackageDirty`；找不到字段时错误信息带字段名；`BuildTags` 前移；修正缩进

- feat(unreal): `get_asset_struct` 移除 `detail`/顶层 `assetType`/`name`；新增 `propertyPaths` 过滤字段；`ToPinType()` 一次调用；补 `subCategoryObject`（对象/结构引用的具体类名）；新增 `fieldCount`；头文件去 `NexusAssetUtils.h` 依赖

- fix(unreal): `manage_asset_material` 校验 `Arguments`、非空 `operations`；`set_param` 补 `PostEditChange`/`MarkPackageDirty` 及 vector 格式校验、texture 改包路径；`FindOutputIdx`/`FindInputIdx` 找不到时返回 -1 并 error（不再静默误路由）；`add_node` 优先 `FindObject` 查类，回退迭代器加 4096 上限；`BuildTags` 前移；修正 L366 缩进

- fix(unreal): `get_asset_material` 修复 `includePins=false` + `includeWires=true` 时连线数据丢失；overview 的 `parentMaterial` 改为包路径；`assetPaths[]` 无效项改为 error entry（不再静默跳过）；graph 改为先过滤再分页再序列化

- fix(unreal): `create_asset_material` 增加 `DoesPackageExist` / `IsValidLongPackageName(FText)`；`type` / `materialDomain` 走 schema Enum；`materialDomain` 与 `EMaterialDomain` 对齐（含 `volume` / `runtimeVirtualTexture`）且非法时先于创建失败；`path`/`parentMaterial` 改为包路径；父加载成功但类型非 `UMaterialInterface` 时单独报错；成功返回 `success`；`BuildTags` 前移；拆分 `inheritedParameters` 收集逻辑

- fix(unreal): `manage_asset_data_table` 校验 `Arguments`、非空 `rows`；`add`/`set` 使用 `ImportTextFromString`；`add` 的 `fields` 支持 number/bool/null；`set` 失败回滚旧值；仅真实变更后 `MarkPackageDirty`；去掉校验失败时的误标脏；`assetPath` 未找到时 `fatalError` 带路径

- fix(unreal): `manage_asset_data_asset` 拒绝空 `ops`；`set` 使用 `ImportTextFromString` 校验解析；`reset` 改为从类 CDO `CopyCompleteValue`（与编辑器恢复默认一致）；有成功变更时末尾单次 `MarkPackageDirty`；新增 `FNexusPropertyUtils::ImportTextFromString`

- feat(unreal): `get_asset_data_table` 新增 `mode`（`auto`|`schema`|`rows`，默认 `auto`）显式区分结构/行两种路径；`propertyPaths` 列与行字段首段过滤；移除 `detail` 与顶层 `assetType`/`name`/`path`；精简关键词

- feat(unreal): `get_asset_data_asset` 支持 `propertyPaths`、属性 `value`/`inherited` 与 `ExportText`；移除顶层 `assetType`/`name`/`path` 与 `detail` 入参（输出为 `class`、分页与 `properties[]`）；精简头文件依赖与搜索关键词

- fix(unreal): `create_asset_data_table` 必填校验、`DoesPackageExist`、包名 `IsValidLongPackageName(FText)`、行结构体 `F` 前缀与 `NativeFirst` 解析、精简关键词

- refactor(unreal): `create_asset_data_table` 成功响应不再返回 `path`、`rowStruct` 字段

- fix(unreal): `create_asset_data_asset` 使用 `DoesPackageExist` / `IsValidLongPackageName`、父类解析改用 `FindClassWithUPrefix`、抽象类报错去掉全表 `TObjectIterator`、`path` 改为包路径、精简搜索关键词
- fix(unreal): UE4.26 编译——`IsValidLongPackageName` 原因参数使用 `FText`；`manage_asset_blueprint` 的 `isPublic` 在 `NX_UE_HAS_CPF_BLUEPRINT_READWRITE` 为假时用 `CPF_BlueprintVisible` 并清除 `CPF_BlueprintReadOnly`

- docs(unreal): 修正 `InitializeInstructions.md`（无效工具名与工作流、`get_asset_material` 的 `sections` 写法、`widget_property_write` 与 `interact_widget` 的区分）

- refactor(unreal): 新建 `FNexusMultiSectionCapability` 中间基类（继承 `FNexusCapability`），将 multi-section 框架（`GetSectionNames` / `GetDefaultSectionNames` / `PrepareEntry` / `ExecuteSection` / `ExpandPerEntry` / `RunMultiSection` / sections schema 注入）从 `FNexusCapability` 剥离；`FNexusCapability` 恢复纯虚 `Execute()=0` 干净状态；迁移 `get_asset_blueprint` / `get_asset_anim_blueprint` / `get_actor_animation` / `get_gameplay_tags` / `get_asset_material` 五个 cap 改继承新基类，统一用 `sections: string[]` 入参（支持 `"all"` 与默认列表），废弃各自的 `section: string` 入参；`get_asset_anim_blueprint` / `get_actor_animation` / `get_asset_material` 新增 `assetPaths[]` / `actorNames[]` 批量展开支持

- refactor(unreal): `FNexusCapabilityRegistry` 单例化 + 元数据预算缓存——引入 `FCapRecord`（Def + Keywords + Instance）与 `NameIndex`（`TMap`），注册与查找均降至 O(1)；`search_capabilities` 每次查询从 70×Factory()+ParseIntoArrayWS 降至纯只读 record 访问；`InputSchema` 注册时 deep-clone 防污染；`bForceDisabled` / `IsForceDisabled()` 已删除（无 cap 实际使用）
- refactor(unreal): `Capability::Execute` 改返回 `FCapabilityResult{Entries/TopFields/FatalError}`，统一单出口——废弃三出口 `(OutEntries, OutTop, OutCapabilityError)`；`Run()` 内置 Args 兜底、required 字段自动校验、执行计时 UE_LOG；基类新增 `RequireString` / `EmitEntry` / `EmitError` 静态样板 helper（新 cap 推荐使用）
- chore(unreal): 删除未使用的 `bForceDisabled` 字段及 `IsForceDisabled()` 虚函数

- refactor(unreal): `FGetAssetWidgetTreeCapability` 重命名为 `FGetAssetUserWidgetCapability`，文件改为 `NexusGetAssetUserWidgetCapability`（工具名 `get_asset_widget_tree` 不变）

- refactor(unreal): 将 `Capabilities/Asset/Widget/` 目录重命名为 `Capabilities/Asset/UMG/`；`FCreateAssetWidgetCapability` 重命名为 `FCreateAssetUserWidgetCapability`（工具名 `create_asset_widget` 不变）

- refactor(unreal): 将 `get_asset_slate_widget` 移入 Runtime 目录并重命名为 `get_slate_widget`（`FGetSlateWidgetCapability`），旧工具名不再可用

- refactor(unreal): 将 `manage_asset_material_wires`（connect/disconnect/disconnect_all）合并入 `manage_asset_material` 的 `operations[]`；删除 `FManageAssetMaterialWiresCapability` 及对应文件，旧工具名不再可用

- feat(unreal): 新增 `manage_asset_data_asset` Capability，支持批量 `set`/`reset` DataAsset 可编辑属性值
- refactor(unreal): 将 `get_asset_material_function` 合并入 `get_asset_material`，后者现统一处理 Material / MaterialInstance / MaterialFunction 三种资产类型；删除 `FGetAssetMaterialFunctionCapability` 类及对应文件

- **BREAKING** refactor(unreal): 合并 `manage_asset_data_table_row`（add/remove）与 `set_asset_data_table_row`（set）为统一的 `manage_asset_data_table`，`action` 新增 `set` 值，`rows[]` 项额外支持 `fieldName`/`value`；旧工具名不再可用

- refactor(unreal): 将 `FGetActorPropertyCapability`、`FSetActorPropertyCapability`、`FGetWidgetPropertyCapability`、`FSetWidgetPropertyCapability` 从 `Capabilities/Property/` 移入 `Capabilities/Runtime/`；同步删除 `FGetAssetPropertyCapability` / `FSetAssetPropertyCapability`（`asset_property` / `asset_property_write`）及 `Property/` 目录

- refactor(unreal): Asset 目录下所有 Capability 文件/类名统一加 `Asset` 中缀（`NexusCreate*` → `NexusCreateAsset*`，`NexusManage*` → `NexusManageAsset*`，共 21 个文件对），与 `NexusGetAsset*` 系列命名对齐

- feat(unreal): 新增 `manage_anim_blueprint` Capability — 管理 AnimBlueprint 状态机：add/remove `state_machine` / `state` / `transition`（同步 EdGraph + `CompileBlueprint` 重建 BakedStateMachines；entry→first state 初始连线需用 `add_transition` 显式创建）；新增辅助 `FNexusAnimGraphUtils`（状态机/状态/转换查找）；编辑器段新增 `AnimGraph` 模块依赖（不影响发行包）
- feat(unreal): 新增 `manage_behavior_tree` Capability — 管理 BehaviorTree 资产节点树（set_root/add_node/remove_node/add_decorator/remove_decorator/add_service/remove_service），节点通过点分路径定位；仅修改运行时数据层，编辑器图不同步
- feat(unreal): 新增 `manage_blackboard` Capability — 批量增删/重命名 BlackboardData Key，支持 bool/float/int/string/name/vector/rotator/object/class 九种类型；`assetPath` 接受 BB 资产路径或 BT 资产路径（自动取关联 BB）
- feat(unreal): 新增 `manage_anim_montage` Capability — 管理 AnimMontage 资产的 Segment（add_segment/remove_segment）和 Section（add_section/remove_section）；add_segment 自动追加至 slot 末尾；add_section 按时间排序插入
- feat(unreal): 新增 `get_asset_anim_blueprint` Capability — 读取 AnimBlueprint 资产的变量列表（name/type/category）和状态机结构（name/stateCount/states[]）
- feat(unreal): 新增 `get_asset_anim_montage` Capability — 读取 AnimMontage 资产的 Slot/Segment 列表和 Section 列表

- **BREAKING** refactor(unreal): 统一 `Capabilities/Asset` 下 8 组 Capability 的类/文件名与 MCP 标识（`GetName()`，亦即 `tools/call` 与 `call_capability` 的 capability 名）：`search_assets`→`search_asset`；`get_widget_tree`→`get_asset_widget_tree`；`get_slate_widget`→`get_asset_slate_widget`；`get_data_table_row`→`get_asset_data_table_row`；`set_data_table_row`→`set_asset_data_table_row`；`manage_data_table_row`→`manage_asset_data_table_row`；`blackboard_asset`→`get_asset_blackboard`；`behavior_tree_asset`→`get_asset_behavior_tree`。对应实现：`NexusSearchAssetCapability`、`NexusGetAssetWidgetTreeCapability`、`NexusGetAssetSlateWidgetCapability`、`NexusGetAssetDataTableRowCapability`、`NexusSetAssetDataTableRowCapability`、`NexusManageAssetDataTableRowCapability`、`NexusGetAssetBlackboardCapability`、`NexusGetAssetBehaviorTreeCapability`。已保存的按 capability 名禁用项需按新名重配。上述 8 个 cap 的 `GetExtraSearchKeywords` 不再包含旧 MCP 工具名字面量。
- feat(unreal): 设置面板「MCP Capabilities」列表按 `Private/Capabilities/**` 下 `*Capability.cpp` 的相对路径**多级折叠**（每段目录一层 `SExpandableArea`，标题为当前层中文名；父级勾选作用于子树内全部可切换项）；找不到源码或解析失败时回退为原 `FNexusMcpTags` + `GetCategoryMapping()` 中文分类；无匹配归入「其他」。
- refactor(unreal): `Struct` / `Widget` 相关 Capability 源码由 `Private/Capabilities/Struct/`、`Private/Capabilities/Widget/` 迁至 `Private/Capabilities/Asset/Struct/`（`create_struct` / `get_asset_struct` / `manage_struct_field` 共 3 对）、`Private/Capabilities/Asset/Widget/`（`create_widget` / `get_asset_slate_widget` / `get_asset_widget_tree` / `manage_widget` 共 4 对）；`register_capabilities.py` 最深段 `Struct`/`Widget` 推断 `FNexusMcpTags::Struct`/`Widget` 不变。
- refactor(unreal): 材质相关 Capability 源码由 `Private/Capabilities/Material/` 迁至 `Private/Capabilities/Asset/Material/`（`create_material` / `get_asset_material` / `get_asset_material_function` / `manage_material` / `manage_material_wires` 共 5 对）；`register_capabilities.py` 仍按路径最深段 `Material` 推断 `FNexusMcpTags::Material`，`BuildTags` 语义不变。
- refactor(unreal): `DataAsset`/`DataTable` 相关 Capability 源码由 `Private/Capabilities/DataAsset/` 迁至 `Private/Capabilities/Asset/DataAsset/`（7 对）；`register_capabilities.py` 仍按路径最深段 `DataAsset` 推断 `FNexusMcpTags::Data`，`BuildTags` 语义不变。
- refactor(unreal): 蓝图相关 Capability 源码由 `Private/Capabilities/Blueprint/` 迁至 `Private/Capabilities/Asset/Blueprint/`（`create_blueprint` / `get_asset_blueprint` / `manage_blueprint_*` 共 6 对）；`register_capabilities.py` 仍按路径最深段 `Blueprint` 推断 `FNexusMcpTags::Blueprint`，`BuildTags` 语义不变。
- feat(unreal): 新增 Capability `create_blackboard`（`FCreateBlackboardCapability`）— 独立创建空 `BlackboardData` 并尝试落盘。
- **BREAKING** refactor(unreal): 移除 Capability **`get_asset_generic`**（删除 `NexusGetAssetGenericCapability.{h,cpp}`）；无专属 `get_asset_*` handler 的资产请改用 **`get_property`**（asset 目标）枚举/钻取反射字段。
- **BREAKING** feat(unreal): MCP 能力 **`list_assets`** 更名为 **`search_assets`**（`FSearchAssetsCapability`，`NexusSearchAssetsCapability.{h,cpp}`，删除 `NexusListAssetsCapability.*`）；新增可选 **`query`**（空白分词 AND，每词须在 `name`/`path`/`assetType`/`parentClass`/`rowStruct`/`parentMaterial` 至少一处命中，规则同 `FNexusStringMatchUtils`）；与 `nameFilter` 及 `assetType`/`pathFilter` 可叠加。`BuildTags`：Readonly + Editor。
- refactor(unreal): 材质表达式共用辅助由 `Tools/Material/NexusMcpToolMaterialCommon.h`（`FNexusMaterial`）迁至 `Public/Utils/NexusMaterialUtils.h`（`FNexusMaterialUtils`）。
- **BREAKING** refactor(unreal): 移除 MCP 工具 **`compile_blueprint`**（删除 `FCompileBlueprintCapability` / `NexusCompileBlueprintCapability.{h,cpp}`）；蓝图/控件树批量编辑仍由各 `manage_*` 与 `set_property` 路径在结束时重编译，持久化用 `save_asset`。
- **BREAKING** refactor(unreal): `call_capability` 直调行为树运行时能力由 `behavior_tree_runtime` 更名为 `actor_behavior_tree`（类 `FGetActorBehaviorTreeCapability`，`Capabilities/Runtime/NexusGetActorBehaviorTreeCapability.{h,cpp}`）。
- refactor(unreal): 将原 `get_behavior_tree` 单 Capability 拆为 `behavior_tree_asset` / `blackboard_asset` / `actor_behavior_tree` 三个 Capability（`FGetBehaviorTreeAssetCapability` / `FGetBlackboardAssetCapability` / `FGetActorBehaviorTreeCapability`）；BT 节点 JSON 构建提取至 `FNexusBehaviorTreeInspectUtils`；新增 `FNexusMcpToolGetBehaviorTree`（`REGISTER_MCP_TOOL`）保持 `get_behavior_tree` 工具名与 `section` 参数不变。
- refactor(unreal): 行为树资产相关 Capability 与 `get_behavior_tree` 工具源码由 `Capabilities/BehaviorTree/`、`Tools/BehaviorTree/` 迁至 `Capabilities/Asset/AI/`、`Tools/Asset/AI/`；`register_capabilities.py` 目录分类以 `AI` 替代 `BehaviorTree`（嵌套 `Asset/AI` 仍推断为 Blueprint）。
- **BREAKING** refactor(unreal): 移除 Capability **`call_blueprint_function`**（删除 `NexusCallBlueprintFunctionCapability.{h,cpp}`）；PIE 下调 Actor 蓝图无参函数请改由项目内自定义通道（如 UnLua `manage_lua`、控制台、`exec_command`）实现。
- **BREAKING** refactor(unreal): 拆分 `manage_asset` 为原子能力 **`delete_asset`**（`FDeleteAssetCapability`）与 **`rename_asset`**（`FRenameAssetCapability`）；移除 `FManageAssetCapability`。调用方用 `delete_asset(assetPath)` / `rename_asset(assetPath,newPath)`；批量删除需多次调用或自行循环。
- refactor(unreal): `create_anim_blueprint` / `create_anim_montage` Capability 迁至 `Private/Capabilities/Asset/Animation/`（原顶层 `Capabilities/Animation/` 仅余此二创建类，与资产创建归类一致）；`register_capabilities.py` 目录→分类推断改为自深向浅匹配，嵌套 `Asset/Animation` 仍映射为 Blueprint。
- refactor(unreal): 移除运行时工具 `manage_animation`（删除 `NexusManageAnimationCapability.{h,cpp}`）；蒙太奇播停请改由项目内蓝图或 Lua / 控制台等既有通道实现。
- refactor(unreal): `get_actor_animation` Capability 源码由 `Capabilities/Animation/` 迁至 `Capabilities/Runtime/`（与运行时工具同目录）。
- refactor(unreal): `get_animation` 重命名为 `get_actor_animation`；参数改为单个必填 `actorName`（移除 `actorNames[]` 批量）。
- refactor(unreal): Capability 与 Tool 完全解耦 —— `FNexusCapabilityRegistry` 去掉 `HostToolName` 维度改为扁平注册表（重名 `ensureMsgf` 提示）；`REGISTER_MCP_CAPABILITY(CapClass)` 宏移除 host 参数；`FNexusCapabilityDefinition` 新增 `Tags` / `bForceDisabled`，`FNexusCapability` 新增 `virtual void BuildTags()` / `virtual bool IsForceDisabled()` 钩子；`UNexusLinkSettings::IsCapabilityEnabled`/`SetCapabilityEnabled` 入参改为单一 cap 名（`MakeCapabilityKey` 删除）；设置面板 `MCP Capabilities` 改为按 cap 自身 tags 分组（七大分类 + "其他"兜底），ForceDisabled 由 cap 自身决定；`search_capabilities` 输出去掉 `hostTool` 字段、按 cap 自身 tags / 设置过滤；`call_capability` 入参仅 `capability` + `arguments`，按 cap 名直查（重名由注册期 ensure 阻止），错误文案对齐 `Capability 'name' is disabled/force-disabled`。
- feat(unreal): 全量 71 个 Capability 子类自注册到全局表并补齐 `BuildTags()` —— 7 个 `get_asset_*` 手工补；其余 64 个由 `Script/register_capabilities.py` 一次性扫 `Capabilities/**/*.cpp` 末尾追加 `REGISTER_MCP_CAPABILITY(<class>)` + `BuildTags()`（按目录推断功能分类：`AI`/`Animation` → blueprint，`Asset`/`Editor`/`Property` → editor，`Blueprint` → blueprint，`DataAsset` → data，`Lua` → runtime，`Material` → material，`Runtime` → runtime，`Struct` → struct，`Widget` → widget；按 cap 名前缀 `create`/`manage`/`set`/`spawn`/`destroy`/`compile`/`save`/`exec`/`control`/`interact` 推断 `Write`，其余 `Readonly`；`get_actor_property` 等 11 个跨目录特例显式覆盖）。Lua cap 追加块自动包在原 `#if WITH_UNLUA` 守卫内；`FGetEditorInfoCapability::GetExtraSearchKeywords` 顺手解开误置的 `#if NX_UE_AT_LEAST(5, 0)` 守卫（修 4.26 链接错）。
- feat(unreal): 6 个 `get_asset` 系列 Capability 名改为全局唯一 `get_asset_<type>` 形式（`get_asset_blueprint` / `get_asset_material` / `get_asset_material_function` / `get_asset_struct` / `get_asset_data_asset` / `get_asset_data_table`；曾含 `get_asset_generic` 后已移除，见上条 BREAKING）。
- refactor(unreal): 删除"工具列表"用户配置 —— 移除 `UNexusLinkSettings` 的 `DisabledTools`/`KnownToolNames`/`bToolDefaultsApplied` UPROPERTY 与 `IsToolEnabled`/`SetToolEnabled`/`NotifyToolsChanged`/`EnsureDefaultToolMode` API；删除 `FNexusLinkSettingsCustomization`（设置面板树状工具列表 UI）及 `NexusLink.cpp` 的 `PropertyEditor` 注册；删除 `FNexusMcpServer::BroadcastToolsChanged`（孤儿 API）。`tools/list`、`tools/call`、`search_capabilities`、`call_capability` 的可见性/可调用性改为仅由 `ENexusToolVisibility::ForceDisabled`（编译期硬禁用，如 UnLua 缺失）决定；`FNexusMcpToolRegistry::GetEnabledDefinitions` 改为按 `Visibility != ForceDisabled` 过滤，新增 `FindDefinition()` 供调用方按名查 Visibility。`Build.cs` 移除 `PropertyEditor` 编辑器依赖。
- refactor(unreal): 简化 `call_capability` 工具 —— 入参移除 `hostTool`，仅保留 `capability` 名 + `arguments`；按 cap 名全局检索（跨所有已启用宿主），歧义时返回工具级错误并列出候选宿主；输出去掉 `totalCount`/`successCount`/`failCount`/`capabilityErrors[]` 包裹层，直接透传 cap 写入 `OutTop` 的字段（多 entry 时附 `results[]`）。
- refactor(unreal): 简化 `search_capabilities` 工具 —— 入参仅保留 `query`（一句话意图描述），移除 `hostTool`/`detail` 参数；输出固定返回 `hostTool`/`name`/`description`/`parameters`（含 `name`/`type`/`description`/`required`）；将内部匹配和参数提取逻辑提取为 `FNexusMcpToolSearchCapabilities` 的 `private static` 方法。
- feat(unreal): `FNexusCapability` 新增 `virtual GetSearchKeywords()`——默认对 Name/Description 分词去重，子类可 override 追加业务语义词；`search_capabilities` 改用此方法做 AND 匹配，不再直接比较原始字符串。
- perf(unreal): `search_capabilities` 匹配从二元 AND 改为分级评分排序——完全相等 +10 / 前缀 +5 / 子串 +2（token 长度 ≥ 3），全 token 命中再 +10 加成；结果按 `score` 降序返回（同分稳定保持注册顺序），输出体新增 `score` 字段。
- refactor(unreal): `FNexusCapability::GetSearchKeywords()` 改为模板方法，子类不再 override 此函数；新增 `virtual GetExtraSearchKeywords()` 让子类只声明特有的同义词/领域词，基类自动合并 Name/Description 分词、转小写、去重。
- feat(unreal): 为全部 71 个 Capability 子类补齐 `GetExtraSearchKeywords()`——按模块（Animation/Asset/Blueprint/DataTable/Editor/Lua/Material/Property/Runtime/Struct/Widget）补充动作词与领域同义词，显著提高 `search_capabilities` 命中率。

- refactor(unreal): 移除 `FNexusCapability::IsMatched` 纯虚方法及全部 cap 实现；删除 `FNexusMcpTool::EnsureCapsBuilt` / `MergeCapabilitySchemas` / Path B（Tool 路由 Capability）；删除 `FNexusCapabilityRegistry::AppendForTool`；Capability 现统一通过 `search_capabilities` / `call_capability` 元工具索引和驱动，不再绑定到 Tool。

- refactor(unreal): `Public/Utils` 与 `Private/Utils` 下工具容器由 `struct` 改为 `class`（静态 API 置于 `public:`）。
- refactor(unreal): 删除 `NexusGetAssetCommon.{h,cpp}`；`Public/Utils/NexusAssetUtils.h` 提供 `FNexusAssetUtils` 静态方法（`MatchesPropertyPathsFilter` / `ApplyDetailMinimal`）；`FNexusPropertyUtils` / `FNexusLuaUtils` 内联工具改为同类静态方法；各 get_asset Cap 直接继承 `FNexusCapability`。
- refactor(unreal): `NexusGetAssetUtils` / `FNexusGetAssetUtils` 更名为 `NexusAssetUtils` / `FNexusAssetUtils`。
- refactor(unreal): 移除 `FNexusPropertyUtils::LoadAsset`（与 `FNexusAssetUtils::LoadAssetWithFallback<UObject>` 重复），UObject 路径加载统一经 `FNexusAssetUtils`。
- refactor(unreal): 将原 `NexusAssetLoadUtils.{h,cpp}` 并入 `NexusAssetUtils` / `FNexusAssetUtils`；Automation 测试 `NexusLink.Utils.AssetUtils.FindClass`（原 `AssetLoadUtilsTests.cpp` → `NexusAssetUtilsTests.cpp`）。

- feat(unreal): 新增元工具 `search_capabilities`（`query`/`hostTool`/`detail`），按宿主 MCP 工具名与关键词枚举 `FNexusCapabilityRegistry` 中已注册且宿主已启用的 Capability；`FNexusCapabilityRegistry::EnumerateFactories` 供稳定顺序遍历。

- feat(unreal): 新增元工具 `call_capability`（`hostTool`/`capability`/`arguments`），按名工厂化单条 Capability 并 `Run`（跳过宿主 `IsMatched`）；`FNexusCapabilityRegistry::CreateCapabilityByName`。

- refactor(unreal): 移除元工具 `search_tools`（`NexusMcpToolSearchTools`）及 `NexusMcpDispatcher::LoadToolHelpMap`；运行时不再加载 `tool-help.json`（`build_tool_help` 生成文件仍随 Resources 分发供外部分析）。
- refactor(unreal): 删除 `Resources/tool-aliases.json`、`Resources/tool-help.json` 及生成脚本 `nexus-script/build_tool_help.py`；`build_all.py` 移除对应预处理步骤。

- refactor(unreal): 将 `FNexusRuntime`（`Tools/Runtime/NexusMcpToolRuntimeCommon`）迁移为 `FNexusRuntimeUtils`（`Utils/NexusRuntimeUtils`），与 `FNexusPropertyUtils` 等工具类统一归入 `Utils/`，同步更新 17 个 Capability 引用文件。
- refactor(unreal): 删除整个 feedback 子系统——移除 `Feedback/` 目录（Collector/Aggregator/Reporter/Internal）、三个 feedback MCP 工具（submit_feedback/export_feedback/report_unused_fields）及 Capability 文件、DispatchObserver 观察者接口、设置面板 AI 反馈分类 UI、`bEnableFeedback` 等五个 UPROPERTY、`IsFeedbackTool` 逻辑、Dispatcher Notify 调用链、NexusMcpToolSearchTools 的 RecordManual 埋点、tool-aliases.json 三条目及两个 L1 测试文件。

- refactor(unreal): 移除 `tools/list` 暴露模式（`ENexusToolsListMode` 枚举、`ToolsListMode`/`StarterToolNames` 设置项、`DefaultStarterToolNames` 启动套件数组），`HandleToolsList` 改为始终暴露全部已启用工具；`/status` 响应移除 `toolsListMode` 字段；`initialize.instructions` 不再按模式动态注入 Tool discovery 行；反馈 JSONL 条目移除 `toolsListMode` 字段。

- refactor(unreal): 将 Widget/BehaviorTree/Struct/DataAsset/Editor 下剩余旧式 Capability（`ApplyOne`/`OnBegin`/`ExtractItems`/`GetArrayKey`/mutable 缓存字段）全部迁移至纯虚 `Execute` 接口；同步更新 `CapabilityTests.cpp` 测试 fixture，移除已废弃的基础设施测试用例（SingularFallback/AliasInjection/LocatorAutoFill）；修复 `NexusGetBehaviorTreeCapability.cpp` 中 `BT->RootTask`→`BT->RootNode` 及缺失 `EngineUtils.h` 导致的编译错误。全版本 UE_4.26～UE_5.7 PASS=9, FAIL=0, WARNINGS=0

- refactor(unreal): 彻底去除 `FNexusCapability` 批处理基础设施——删除 `GetArrayKey`/`GetSingularKey`/`ExtractItems`/`GetItemAliasFields`/`GetLocatorField`/`OnBegin`/`OnEnd`/`ApplyOne` 全部钩子；新增纯虚 `Execute(Arguments, OutEntries, OutTop, OutError) const`，`Run()` 变薄壳直调；移除所有 `mutable CachedArgs/CachedPaths` 等字段；全部 70+ 个 Capability 子类迁移完成，每个 Capability 只处理单条输入（单 `assetPath`/`actorName`/`widgetName` 等），禁止内部循环批量数组。
- refactor(unreal): 单条化涉及接口变更——`spawn_actor` 移除 `spawns[]` 改为直接接收 `blueprintPath/className` + 位置/旋转；`destroy_actor` 从 `actorNames[]` 改为 `actorName`；`interact_widget` 移除 `operations[]` 改为直接接收 `widgetName/action/value/ownerWidget`；`get_asset_refs` / `compile_blueprint` 从 `assetPaths[]` 改为 `assetPath`；`get_property`（actor/widget/asset 三分支）从 `actorNames[]/widgetNames[]/assetPaths[]` 改为单数；`diff_actors` 移除批量基线模式（仅保留 `actorNameA/B` 对模式）。
- refactor(unreal): `NexusGetAssetCommon.h` 移除 `NexusExtractAssetPaths` 辅助函数（批量路径提取，已不需要）；`NexusUnreal规范.mdc §1.2` 移除 `FNexusBatchMcpTool` 引用、`§1.5` 批量参数硬约束更新为 Capability 单条处理约束。

- **BREAKING** refactor(unreal): `get_asset` 工具能力收窄为通用反射兜底 —— Blueprint / DataTable / UserDefinedStruct / Material / MaterialInstance / MaterialFunction / DataAsset 的专属解读 Capability 已物理拆分到独立文件但**暂未注册**到任何 Tool（保留实现以备未来挂到独立的 `get_<type>` 工具）。曾阶段性只挂 `FGetAssetGenericCapability` 作反射兜底（**该 cap 已删除**，见 `[Unreleased]` 顶栏；无 handler 资产改 `get_property`），对任意 UObject 曾返回 `{assetType, name, path, properties:[{name, type}], totalCount, offset, limit, count}`；BP 不再返 `graphs[]` / `luaModule` / `luaFilePath`，Material 不再返 `parameters[]` / `materialDomain`，DataTable 不再返 `rowNames[]` / `columns[]`，Struct 不再返 `fields[]`，连带 `section` / `graphName` / `graphType` / `propertyPaths` / `categoryFilter` / `isPublicOnly` / `includePins` / `includeWires` 等专属参数全部从 schema 移除。`nexus-ai/rules/NexusMCP调用规范.mdc §4` 与 `docs/tool-reference.md` 中相关 section 用法描述待后续同步。
- refactor(unreal): `NexusGetAssetCapability.cpp` 单文件 755 行按资产类型拆为 7 个独立 Capability + 1 个公共基类 + 1 个 Material Expression Graph 共享头：新增 `Capabilities/Asset/` 下 `NexusGetAssetCommon.{h,cpp}`（`FGetAssetQueryParams` / `NexusParseGetAssetQueryParams` / `NexusMatchesPropertyPathsFilter` / `FGetAssetCapabilityBase` 公共基类，统一处理 assetPaths 批量、Load 失败兜底、AcceptsAsset 类型校验、`detail=minimal` 收窄）、`NexusGetAssetMaterialShared.{h,cpp}`（Material/MaterialFunction 复用的 expression graph 序列化）、6 个 cap 文件对（`Blueprint` / `DataTable` / `Struct` / `Material` / `MaterialFunction` / `DataAsset`；`Generic` 曾独立为 `NexusGetAssetGenericCapability` 后已删）。删除原 `NexusGetAssetCapability.{h,cpp}`。UE_4.26 编译 PASS=1, FAIL=0, WARNINGS=0
- refactor(unreal): 删除已无子类的 `FNexusBatchMcpTool` 中间层（`NexusBatchMcpTool.h/.cpp`），清理 `NexusMcpTool.h` 注释残留。
- fix(unreal): 将 Lua cap 间重复的 `GetUnLuaModuleName`/`CollectTableKeys`/`PushLoadedModule` 提升为 `NexusLuaUtils.h` 中的 `inline` 函数（`NexusGetUnLuaModuleName`/`NexusCollectTableKeys`/`NexusPushLoadedModule`）；将 Property cap 间重复的 `ReadStringArray`/`NexusPeekFirstUpdate` 提升为 `NexusPropertyUtils.h` 中的 `inline` 函数（`NexusReadStringArray`/`NexusPeekFirstUpdate`），消除 Unity Build 下的 C2084 重复定义错误。
- refactor(unreal): `Material` / `Widget` / `Struct` 目录下全部 8 个工具（`create_material` / `manage_material` / `manage_material_wires` / `create_widget` / `get_widget_tree` / `manage_widget` / `create_struct` / `manage_struct_field`）实现迁移到 `FNexusCapability` 编排。新增 8 个 cap 文件对（`Private/Capabilities/{Material,Widget,Struct}/Nexus*Capability.{h,cpp}`）；原 `FNexusBatchMcpTool` 子类（3 个 Manage* 工具）改继承 `FNexusMcpTool`；`manage_material_wires` 的 `OnEnd::PostEditChange` 移入 `ApplyOne` Finalize 段；`manage_widget` 的 `OnEnd::MarkPackageDirty` 移入 `ApplyOne` MarkDirty 段；`manage_struct_field` 的 `OnEnd::CompileStructure + MarkPackageDirty` 移入 `ApplyOne` Finalize 段；所有工具主类头文件移除 `BuildSchema` / `ExecuteImpl` / `OnBegin` / `ApplyOne` / `OnEnd` / `GetArrayKey` 声明，cpp 瘦身为仅含 `GetName/GetDescription/BuildTags` 三钩子。UE_5.7 编译 PASS=1, FAIL=0, WARNINGS=0
- refactor(unreal): `DataAsset` 目录下全部 5 个工具（`create_data_asset` / `create_data_table` / `get_data_table_row` / `set_data_table_row` / `manage_data_table_row`）实现迁移到 `FNexusCapability` 编排。新增 5 个 cap 文件对（`Private/Capabilities/DataAsset/Nexus*Capability.{h,cpp}`）；原 `FNexusBatchMcpTool` 子类（3 个 *Row 工具）改继承 `FNexusMcpTool`；`OnEnd` 中的 `MarkPackageDirty` 移入各 `ApplyOne` Finalize 段；工具主类头文件移除所有实现声明，cpp 瘦身为仅含 `GetName/GetDescription/BuildTags` 三钩子。UE_5.7 编译 PASS=1, FAIL=0, WARNINGS=0
- refactor(unreal): `Blueprint` 目录下全部 9 个工具（`call_blueprint_function` / `compile_blueprint` / `create_anim_blueprint` / `create_blueprint` / `list_assets` / `manage_blueprint_component` / `manage_blueprint_graph` / `manage_blueprint_variable` / `manage_blueprint_wires`）实现迁移到 `FNexusCapability` 编排。新增 9 个 cap 文件对（`Private/Capabilities/Blueprint/Nexus*Capability.{h,cpp}`）；原 `FNexusBatchMcpTool` 子类（4 个 Manage* 工具）改继承 `FNexusMcpTool`，所有工具主类头文件移除 `BuildSchema` / `ExecuteImpl` / `OnBegin` / `ApplyOne` / `OnEnd` / `GetArrayKey` 声明，cpp 瘦身为仅含 `GetName/GetDescription/BuildTags` 三钩子。UE_5.7 编译 PASS=1, FAIL=0, WARNINGS=0
- refactor(unreal): `Runtime` 目录下全部 9 个工具（`destroy_actor` / `list_actors` / `list_runtime_widgets` / `spawn_actor` / `spawn_widget` / `diff_actors` / `get_animation` / `manage_animation` / `interact_widget`）实现迁移到 `FNexusCapability` 编排。新增 9 个 cap 文件对（`Private/Capabilities/Runtime/Nexus*Capability.{h,cpp}`）；原 `FNexusBatchMcpTool` 子类改继承 `FNexusMcpTool`，所有工具主类头文件移除 `BuildSchema` / `ExecuteImpl` / `OnBegin` / `ApplyOne` / `OnEnd` / `GetArrayKey` 声明，cpp 瘦身为仅含 `GetName/GetDescription/BuildTags` 三钩子。UE_5.7 编译 PASS=1, FAIL=0, WARNINGS=0
- **BREAKING** refactor: `Editor` 目录下全部 18 个工具（`get_editor_info` / `export_feedback` / `submit_feedback` / `exec_command` / `set_log_capture_filter` / `get_output_log` / `save_asset` / `create_anim_montage` / `create_behavior_tree` / `report_unused_fields` / `get_asset_refs` / `get_slate_widget` / `get_asset` / `capture_viewport` / `control_pie` / `manage_asset` / `get_behavior_tree` / `get_gameplay_tags`）实现迁移到 `FNexusCapability` 编排。新增 18 个 cap 文件对（`Private/Capabilities/Editor/Nexus*Capability.{h,cpp}`），每个工具对应一个 cap；所有工具主类头文件移除 `BuildSchema` / `ExecuteImpl` / `GetVisibility` 声明，cpp 瘦身为仅含 `GetName/GetDescription/BuildTags` 三钩子。输出格式统一为 `{ totalCount, results: [{...}], [capabilityErrors] }`，`export_feedback` 报告体从 `OutputText` 下移到 `results[0].report`（**破坏性变更**）。UE_5.7 编译 PASS=1, FAIL=0, WARNINGS=0
- chore(unreal): 删除 `NexusLuaUtils.h::NexusSerializeLuaResult` 死代码（cap 迁移后所有 cap 直接写 `OutEntry`，不再走 `R.StructuredContent =` 路径）。其余 5 个工具函数（`NexusGetMainLuaState` / `NexusGetLuaEnv` / `NexusSafeLuaNext` / `NexusLuaValueToJson` / `NexusResolveLuaPath`）均为 3-13 个 cap 共用的多用户基础设施，**不**内联到 cap（与 `NexusPropertyTargets` 1:1 翻译层情况不同）；同时把文件从 `Public/Tools/Lua/` 迁移到 `Public/Utils/`，与 `NexusAssetLoadUtils` / `NexusPropertyUtils` / `NexusStringMatch` 等共享基础设施同目录，`Tools/Lua/` 下只保留 Tool 入口 .h
- **BREAKING** refactor: `get_lua` / `manage_lua` 输出契约统一为 `{ totalCount, [successCount, failCount,] results: [{...}], [capabilityErrors: [...]] }`，与 `get_property` / `set_property` 对齐。所有 section/action 分支的旧顶层扁平字段（`depth`/`frames`/`memoryKB`/`resultCount`/`success` 等）下移到 `results[0].xxx`；客户端调用 `r["depth"]` 改为 `r["results"][0]["depth"]`，依此类推。同步更新 `nexus-ai/rules/NexusMCP调用规范.mdc` 速查表与 `Tests/test_95_pie_runtime.py` 用例
- refactor(unreal): `get_lua` / `manage_lua` 实现迁移到 `FNexusCapability` 编排，作为框架第三例（覆盖"单字段枚举分发"形态）。新增 13 个独立 cap 文件（`Private/Capabilities/Lua/Nexus{GetLuaStack,Value,Env,Memory,Metatable,Loaded,Binding,Object,ManageLuaEval,Dofile,Set,Gc,HotReload}Capability.{h,cpp}`），全文件 `#if WITH_UNLUA` 包裹；命中条件按 `section` / `action` 字符串等值，Stack cap 兼任默认分支（缺省 section 视为 stack）；schema 字段按"首注册者拥有权"分散到各 cap（Stack 拥有 section/detail/frame*/sourceFilter/maxDepth；Value 拥有 path；Env 拥有 nameFilter/limit；Binding 拥有 actorName/assetPath；Eval 拥有 action/code；Dofile 拥有 filePath；Set 拥有 path/value；Gc 拥有 mode 枚举），其它 cap 返回 `EmptyObject` 共享上述字段，Tool 默认 BuildSchema 自动并集；每个 cap 在 `OnBegin` 内通过 `NexusGetMainLuaState` 拿 `lua_State*` 并把致命错误（lua 未就绪 / 必填缺失）写入 `OutCapabilityError`；`ExtractItems` 各自注入 1 个 dummy item 触发 `ApplyOne`；13 个 cap 通过 `REGISTER_MCP_CAPABILITY("get_lua", ...)` / `REGISTER_MCP_CAPABILITY("manage_lua", ...)` 自注册。`FNexusMcpToolGetLua` / `FNexusMcpToolManageLua` 删除 `BuildSchema` / `ExecuteImpl`，主类只剩 `GetName/GetDescription/BuildTags/GetVisibility` 四个钩子（cpp 826 → 23 / 497 → 23 行）
- refactor(unreal): 删除 `Tools/Property/NexusPropertyTargets.{h,cpp}`（871 行共享 dispatcher），全部 6 个公共方法及其私有 helpers 直接内联到对应 cap 的 `.cpp` 中：actor 读 → `FGetActorPropertyCapability`（含 `IsDiagnose/SectionPreset` / `WriteActorComponentsSection` / `WriteActorAttachHierarchySection` / `ResolveBatchActorProperties`）、actor 写 → `FSetActorPropertyCapability`、widget 读/写 → 各自 widget cap、asset 读 → `FGetAssetPropertyCapability`（含 `ReadBlueprintProperty`）、asset 写 → `FSetAssetPropertyCapability`（含 `WriteBlueprintProperty`）。该 dispatcher 在 6→6 cap 落地后已无跨 cap 复用价值，删除可消除"中间翻译层"反模式，每个 cap 完整自洽
- refactor(unreal): `set_property` 实现迁移到 `FNexusCapability` 编排，作为框架第二例（与 `get_property` 同模板）。新增 3 个独立 cap 文件（`Private/Capabilities/Property/NexusSet{Actor,Widget,Asset}PropertyCapability.{h,cpp}`），分别覆盖 actor / widget / asset 三分支；命中条件复刻旧 `OnBegin` 推断（target > 顶层 assetPath > 首条 update 内 actorName/widgetName）；Actor cap 承担 `updates[]` 完整 schema（含 propertyPath/value 必填 + 各分支可选键），Asset cap 贡献顶层 `assetPath` 字段，Widget cap 无独有顶层字段；3 个 cap 通过 `REGISTER_MCP_CAPABILITY("set_property", ...)` 自注册到全局表。`FNexusMcpToolSetProperty` 由 `FNexusBatchMcpTool` 改继承 `FNexusMcpTool`，删 `BuildSchema`/`OnBegin`/`ApplyOne`/`OnEnd` 与 `Target/World/AssetObj/AssetPath` 私有状态，主类只剩 `GetName/GetDescription/BuildTags` 三个钩子（cpp 183 → 13 行）。输出格式与历史一致（`successCount/failCount/results[]`，asset 分支顶层回显 `assetPath`），现有用例 `test_95_pie_runtime/test_40_widget/test_30_blueprint/test_50_material` 无须改动
- **BREAKING** refactor: `get_property` 输出契约统一为 `{ totalCount, [successCount, failCount,] results: [...], [capabilityErrors: [...]] }`。旧的"单 actor + section/diagnose 顶层 inline 字段"与"多 actor `{actors:[...]}` 包装"两条特殊路径合并到 `results[]`：`r["components"]/r["hierarchy"]/r["sections"]` → `r["results"][0]["components"]` 等；`r["actors"]` → `r["results"]`。未知 `section`/`diagnose` 不再抛 MCPError，错误写入对应 `results[i].error`，便于多 actor 批量场景部分失败不阻塞其他 entry。docs/tool-reference.md 此前已宣称统一 `results[]` 形态，本次实现对齐文档承诺
- refactor(unreal): `get_property` 实现迁移到 `FNexusCapability` 编排，作为框架首例试点。新增 3 个独立 cap 文件（`Private/Capabilities/Property/NexusGet{Actor,Widget,Asset}PropertyCapability.{h,cpp}`，与 `Private/Tools/` 同级），分别覆盖 actor / widget / asset 三分支；每个 cap 自带 `BuildSchema` 声明本分支独占字段（actor: target/actorName(s)/section/diagnose；widget: widgetNames/ownerClass；asset: assetPath(s)/widgetName；公共 propertyPath/propertyPaths），由 `FNexusMcpTool` 默认 BuildSchema 自动并集成 Tool 入参 schema；Widget/Asset cap 覆写 `ExtractItems` 做 `targets × propertyPaths` 笛卡儿积展开；命中优先级保持旧 `InferTarget` 行为（target > actorName > widgetNames > assetPath）；3 个 cap 在各自 `.cpp` 末尾通过 `REGISTER_MCP_CAPABILITY("get_property", ...)` 自注册到全局表，`FNexusMcpToolGetProperty` 主类对 cap 列表无任何感知；`ExecuteImpl` / `InferTarget` / `ExecuteActor/Widget/Asset` / 自写 `BuildSchema` 全部删除，主类只剩 `GetName/GetDescription/BuildTags` 三个钩子
- refactor: 所有 Tool `.h` 文件从 `Private/Tools/<Category>/` 迁移至 `Public/Tools/<Category>/`，目录结构保持一致；`NexusLink.Build.cs` 对应条目由 `PrivateIncludePaths` 改为 `PublicIncludePaths` 绝对路径
- feat: 引入 `FNexusCapability` 抽象（`Public/NexusCapability.h` + `Private/NexusCapability.cpp`）+ 全局 `FNexusCapabilityRegistry`（`Public/NexusCapabilityRegistry.h` + `Private/NexusCapabilityRegistry.cpp`）+ `REGISTER_MCP_CAPABILITY("tool_name", FXxxCapability)` 自注册宏 —— 一段可批量执行的工作单元，内置数组+单数 fallback / value 字段 alias 注入（`name` / `actorName` / `path` / `assetPath`，子类可扩展） / `GetLocatorField` 自动回填 entry 定位字段 / 致命错误隔离（`OnBegin` 报错只跳过本 Cap，不影响其他）；`FNexusMcpTool` 基类按 `GetName()` 从全局表懒拉取本工具名下所有已注册 cap，`Execute` 自动收集 `IsMatched` 命中的并按注册顺序调用，把 entries 聚合到 `results[]` + 填充 `totalCount` / `successCount` / `failCount` / `capabilityErrors[]`。`ExecuteImpl` 由纯虚改非纯虚（默认返回 `"Tool implements neither ExecuteImpl nor any registered Capability"` 兜底文案），子类既不重写 `ExecuteImpl` 又未注册任何 cap 时不再编译期报错但运行时会立即给出明确信号。Capability 子类**强制实现 5 个纯虚钩子**（`GetName` / `GetDescription` / `BuildSchema` / `IsMatched` / `ApplyOne`）—— `GetDescription` 兜文档/排错语义，`BuildSchema` 让 Tool 默认 BuildSchema 能并集出每个 cap 自带字段，避免 LLM 拿不到分支字段说明。**现有 60+ Tool 子类零行为变化**：注册表无对应 cap → `Execute` 走 `ExecuteImpl` 旧路径；`FNexusBatchMcpTool` 与 `get_property` 等历史工具完全不动。Tool 主类**永远不需要**列举或感知 cap 列表，新增/删除 cap 只改 cap 自己的 `.h/.cpp`，Tool 一行不动。配套 7 条 L1 单元测试 `Source/NexusLinkTests/Private/Tests/CapabilityTests.cpp`（`NexusLink.Capability.*` 过滤器）覆盖 `GetDefinition` 拼装+缓存 / 单数 fallback / alias 注入 / locator 自动回填 / 多 Capability 聚合 / 致命错误隔离 / 默认 ExecuteImpl 兜底；测试通过 `FNexusTestRegisteredCaps` RAII 临时注册 cap，作用域结束自动 `UnregisterAll` 清场
- feat: `FNexusMcpTool::BuildSchema` 由"返回 nullptr"升级为可聚合 Capability schemas 的默认实现 —— Capability 编排工具（注册表挂了 cap）无需再写 `BuildSchema`，基类自动把每个 Cap 的 `GetDefinition().InputSchema.properties` 并集成 `{type:"object", properties:{...}}`；同 key 冲突先到先得 + `LogNexusMcpTool` Warning（避免静默覆盖）；`required` 不自动并集（多 Cap 通常 OR 关系，由运行时 `IsMatched` 决定分支）。同时把"Capability 列表懒拉取"抽成私有 helper `EnsureCapsBuilt()`，`Execute` 与默认 `BuildSchema` 共用。子类需要"共享字段 + Cap 字段"时仍可覆写 `BuildSchema`，并通过新静态方法 `MergeCapabilitySchemas(Caps)` 复用聚合逻辑。L1 测试新增 `NexusLink.Capability.ToolDefaultSchemaAggregate` 覆盖默认聚合 + 同 key 先到先得 + required 不并集
- refactor: `FNexusCapability` 仿 `FNexusMcpTool` 引入 `GetDefinition()` 元数据接口——新增 `FNexusCapabilityDefinition` 结构（`Name` / `Description` / `InputSchema` 三字段，比 Tool definition 精简掉 Tags / Visibility）；子类钩子统一为 `GetName()`（纯虚） / `GetDescription()`（默认空） / `BuildSchema()`（默认 nullptr）；`GetDefinition()` 懒加载缓存（首次调用拼装，后续命中缓存）。原 `GetCapabilityName()` 钩子合并入 `GetName()`，Tool 端 `capabilityErrors[].capability` 字段改读 `GetDefinition().Name`，避免双重命名通道——同一 Cap 实例的"标识符"从此只有一处权威来源
- **BREAKING** refactor: `INexusMcpTool` 接口转为 `FNexusMcpTool` 基类，`GetDefinition` 模板化（基类 final + 缓存，子类按 `GetName` / `GetDescription` / `BuildSchema` / `BuildTags` / `GetVisibility` 五个钩子提供元数据）；`Execute` 拆为 `Execute`(final，懒触发 Definition 缓存) + `ExecuteImpl`(纯虚)；`FNexusBatchMcpTool` 与 73 个工具子类同步迁移，运行时行为零变化
- chore(script): `build_test.{py,bat,sh}` 移除 L1 Automation 触发逻辑（删除 `--run-automation` / `--no-automation` 开关、`run_automation_phase` / `_run_automation_once` / `_parse_automation_output` 与相关常量/落盘文件）。headless 下 MCP HTTP 服务器 socket 噪声会被自动化框架误判为失败，且 L1 用例本就属于"按需手动跑"层级；现 `build_test` 只跑 BuildPlugin + token 预算 lint。L1 改用 `UEEditor-Cmd <uproject> -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi` 手动触发；同步更新 `README.md` / `TEST_CHECKLIST.md` / `NexusUnreal规范.mdc` / `mcp-regression-test` skill
- refactor: `FNexusMcpToolDefinition` 三字段（`bForceDisabled` / `ForceDisabledReason` / `bHideInSettings`）合并为单一枚举 `ENexusToolVisibility`（`Visible` / `ForceDisabled` / `Hidden`），状态空间从 4 态收敛到合法 3 态；强制禁用工具的 tooltip 由 reason 文本回退为工具 `Description`，置灰本身已传达不可用信号
- refactor: 移除 NexusLink 与 NexusLinkTests 全部 `namespace`（具名 + 匿名）。具名 namespace（`NexusMcpTags` / `NexusSchema` / `NexusPropertyUtils` / `NexusAssetLoadUtils` / `NexusStringMatch` / `NexusPinTypeUtils` / `NexusDispatchObservers` / `NexusEditorStatusBar` / `NexusInstanceRegistry` / `NexusFeedbackInternal` / `NexusPortUtils` / `NexusPropertyTargets` / `NexusMaterial` / `NexusRuntime`）→ `struct F<Name>` + 全 static；匿名 namespace（cpp 内部 helper）→ 文件级 `static`；`NexusMcpTags` 常量改 `inline static constexpr` 删除 cpp 端 `extern` 定义；调用点全改 `F<Name>::` 限定调用，所有 `using namespace NexusSchema;` 删除
- feat: `ToolsListMode` 默认值由 `Starter` 改回 `Full`，新安装开箱即全量工具列表；需省 token 时在 Editor Preferences → NexusLink 切换到 `Starter`
- fix(script): `build_test.sh` / `build_test.command` 与 `build_test.bat` 对齐——默认追加 `--run-automation`，`--no-automation` 跳过 L1；Finder 用 `.command` 时亦可从终端传入参数（`"$@"` 转发）
- fix(script): `build_test.sh` 在 macOS 默认 bash 3.2 且 `set -u` 时，空 `REST` 导致 `"${REST[@]}"` unbound，改为 `cmd` 数组组装调用；仓库内为该脚本设置可执行位（`./build_test.sh`）
- docs: `NexusUnreal规范.mdc` 瘦身（`.codebuddy` / `.cursor` 双端同步）——合并重复 cpp 示例、压缩批量参数与 JSON 精简章节、按 `项目规范.mdc §9` 去硬编码漂移数字，行数 561 → 296，字节 33k → 17k

## [1.6.4] - 2026-04-24

- feat: `ToolsListMode` 默认值由 `Full` 改为 `Starter`，新安装用户开箱即省 token；体感差时可在 Editor Preferences → NexusLink → tools/list 暴露模式 切回 `Full`
- perf: `search_tools` 的 `detail=signature` 模式不再输出 `documentation` 字段，减少约 60-80% 响应体量；需文档时使用 `detail=full`
- fix: `ToolDefToJson` 补充输出 `description` 字段，修复 tools/list 响应缺少工具描述导致不符合 MCP 规范的问题
- fix: 测试客户端 `mcp_client.py` 协议版本从 `2024-11-05` 统一为 `2025-06-18`，与服务端声明一致

## [1.6.3] - 2026-04-24

- fix: `bMinimalContentText` 默认值改为 `false`（兼容模式）；Cursor/CodeBuddy 等主流客户端不解析 `structuredContent`，原默认开启导致 AI 只能看到占位摘要而无法读取工具返回内容；需要省 token 且客户端已确认支持 `structuredContent` 时可在 Editor Preferences → NexusLink 手动开启

## [1.6.2] - 2026-04-24

- feat: `search_tools` 大幅提升命中率 — `query` 改为空格分词多词 AND 匹配，每词在 **name / description / documentation / alias**（新增 `Resources/tool-aliases.json` 别名表，覆盖 ~40 个工具的自然语言同义词，如 `screenshot→capture_viewport`、`reference→get_asset_refs`、`reflector→get_slate_widget`）任一子串命中即通过；签名响应新增 `matchedVia` 字段标注命中路径（`name`/`description`/`doc`/`alias`），便于 AI 自检关键词策略
- feat: `initialize.instructions` 的 Starter/Custom 模式工具发现提示扩充，明确 query 多词 AND 语义、可搜索字段范围与 tags 可选值清单，降低 AI 猜测工具名失败率
- perf: 工具响应 JSON 全面改用 condensed 格式（去除换行/缩进），覆盖 `NexusMcpDispatcher`、`NexusPropertyUtils::SerializeJson`、`NexusFeedbackCollector`，减少约 20-40% 输出 token

- fix: `search_tools` 无 `query`/`tags` 过滤时自动降级为 `detail=name`，避免全量返回所有工具 schema+文档导致 MCP 响应过大

- perf: `ToolsListMode=Full` 时 `tools/list` 不再暴露 `search_tools`（全量模式下 AI 直接可见所有工具，无需发现入口）；`Starter`/`Custom` 模式行为不变

- perf: `/status` 接口新增 `toolsListMode` 字段（`full`/`starter`/`custom`），供 rider/vscode 代理动态感知 UE 当前模式

- perf: `search_tools` 设置 `bHideInSettings=true`，不再出现在 UE 设置面板的 MCP 工具列表中（框架内置工具，由 `ToolsListMode` 统一控制）

- feat: `initialize` 响应的 `instructions` 字段按 `ToolsListMode` 动态生成：`Full` 模式告知 AI 所有工具直接可见无需 `search_tools`；`Starter`/`Custom` 模式告知 AI 需先调用 `search_tools` 发现工具

- feat: 反馈 JSONL Entry（`auto`/`manual` 两种 kind）新增 `toolsListMode` 字段（`full`/`starter`/`custom`），便于分析不同模式下的 AI 摩擦点分布

- feat: `search_tools` 未命中（`matchCount=0`）时自动写入 `missing_tool` 反馈，记录 `query`/`tags`/`totalEnabled`，便于分析 Starter 模式下 AI 高频找不到的工具类型

- chore: `ToolsListMode` 默认值由 `Starter` 改为 `Full`，降低 AI 工具发现失败率；Starter 模式作为省 token 的可选配置保留

## [1.6.1] - 2026-04-24

- chore: `check_tool_tokens.py` 适配 Starter 模式 —— 从 `NexusMcpDispatcher.cpp` 动态解析 Starter 套件，对非 Starter 工具放宽预算（param desc ≤80、tool total ≤300、def desc ≤120）；Starter 工具保持严格限制；豁免清单精简为仅 `get_asset`；`list_actors.detail` / `get_asset.detail` 描述缩短至 ≤50 chars

- fix: `FNexusLogCapture::Serialize` 将白名单过滤（`IsAllowed`）移入 `FScopeLock` 内，消除与 `SetCategoryWhitelist` 并发时的 `TArray` 读写竞态
- fix: `create_blueprint` 创建蓝图后立即调用 `UPackage::SavePackage` 落盘，防止编辑器关闭后蓝图丢失；兼容 UE4 旧版 `SavePackage` 签名（`NX_UE_HAS_SAVE_PACKAGE_ARGS`）
- fix: `create_blueprint` 父类解析改用 `EFindFirstObjectOptions::NativeFirst` 精确匹配，并增加 `IsChildOf(UObject)` 类型校验，防止跨模块同名类歧义
- fix: `destroy_actor` 在 `OnBegin` 阶段预收集 `TMap<FString, TWeakObjectPtr<AActor>>` 缓存，`ApplyOne` 从缓存取指针并加 `IsValid` 防御，修复批量传重名 Actor 时第二条误报"not found"的问题
- fix: `manage_blueprint_graph` 所有 `add_node`/`remove_node`/`set_node` 操作前调用 `BP->Modify()`/`Graph->Modify()`/`Node->Modify()`，新节点加 `RF_Transactional`，支持 Ctrl-Z Undo
- fix: `FNexusBatchMcpTool` 对象元素缺少 `value` 字段时，自动从 `name`/`actorName`/`path`/`assetPath` 注入，提升 AI 调用宽容度
- fix: `spawn_widget` World 查找改走 `GEngine->GetWorldContexts()`，避免命中残留的 `bIsTornOff` World
- fix: `NexusMcpServer` 注册 60s 周期 `FTSTicker`（`TickSessionCleanup`）主动清理孤儿 HTTP 会话，防止客户端崩溃后会话无限滞留
- fix: `get_output_log` `textFilters` 字段改用 `TryGetArrayField`，防止 AI 传非数组类型时触发 FJsonObject 断言崩溃

- fix: `GetOrCreateDispatcher` 会话清理策略升级：新增 TTL 清理（握手未完成超 5 分钟、Running 状态空闲超 30 分钟的孤儿会话自动移除），修复 HTTP 会话无限增长的内存泄漏
- fix: `FNexusMcpDispatcher` 新增 `CreatedAt` / `LastActivityAt` 时间戳字段，`Dispatch` 入口自动更新活跃时间，供会话 TTL 清理使用

- perf: `submit_feedback` 纳入 Starter 默认套件，确保反馈工具在 Starter 模式下始终可达，避免"反馈通道被 Starter 限制本身阻断"的循环问题

- feat: `search_tools` `signature`（默认）级别现在同时包含 `tool-help.json` 详细文档，`full` 保留为向后兼容别名；Starter 模式下 AI 无需指定 `detail=full` 即可获取完整工具说明，降低 `No tools matched` 风险
- docs: 完善 `InitializeInstructions.md` Starter 模式工具发现规则，明确握手只发启动套件、任务前须先调 `search_tools` 发现工具、失败时换词重试策略

- fix: `search_tools` 在 `bMinimalContentText=true`（默认）下返回占位摘要而非实际工具列表，导致 Cursor 等不解析 `structuredContent` 的客户端无法使用 Starter 模式渐进发现 — `search_tools` 填写 `OutputText`（condensed JSON），`NexusMcpDispatcher` `bMinimal` 条件补 `OutputText.IsEmpty()` 守卫，确保显式 `OutputText` 始终透传给客户端

- chore: 新增 `Script/measure_token_baseline.py`，建立 MCP Token 优化基线（tools/list 整体尺寸 + 代表工具响应字节/token 估算），结果写入 `Tests/_baselines/token_baseline.json`；当前基线：tools/list 20 工具 12503 字节 ~3364 tokens，get_output_log 100799 字节 ~26157 tokens
- perf: P0 消除响应双份负载 — 新增 `bMinimalContentText` 开关（默认开启），content[0].text 退化为占位摘要，完整数据保留在 structuredContent；新增 pytest 断言验证占位文本格式
- feat: P4 短期 — `build_tool_help.py` 解析 `docs/tool-reference.md` 为 `Resources/tool-help.json`（55 个工具），UE 侧 `LoadToolHelpMap()` 静态加载为 `TMap<FString,FString>` 字典；`build_all.py` 打包流水线自动预处理
- feat: P1 search_tools 渐进披露 + 启动套件 — 新增 `search_tools` 元工具（query/tags/detail 三参数），`ToolsListMode` 三档模式（Full/Starter/Custom），默认 Starter 模式仅暴露 7 件启动套件 + search_tools；`GetEnabledDefinitions()` 抽出共用过滤路径；`InitializeInstructions.md` 增加搜索引导
- perf: P6 TTL 元数据 — 所有成功响应注入 `_snapshotAt`（ISO 8601 UTC），易过期工具（`get_output_log`/`list_actors`/`list_runtime_widgets`/`get_property`/`get_animation`）额外注入 `_ttl_seconds`；仅元数据不裁剪，为未来 IDE 代理层 context-editing 预留
- fix: `ToolsListMode`/`StarterToolNames` 变更后不广播 `notifications/tools/list_changed` 导致 vscode 插件缓存不失效、侧边栏仍显示全量工具 — `PostEditChangeProperty` 补上这两个属性的监听
- perf: P3 detail 参数 — `list_actors`/`get_asset`/`get_behavior_tree` 新增通用 `detail` 参数（`minimal/standard/full`）；`minimal` 仅返回 identity 字段（name/class/path），探索阶段可大幅减少响应体积；`full` 对 `get_asset` 强制 `section=all`；`InitializeInstructions.md` 增加探索阶段优先 `detail=minimal` 引导

## [1.6.0] - 2026-04-23

- test: 新增 `NexusLinkTests/ResponseCompactorTests.cpp`（6 个 UE AutomationTest，共覆盖 **35 个断言**）——纯 C++ 单元测试，无需连接 MCP 或运行 PIE，可在编辑器本地用 Session Frontend 触发：
  - `ScalarTypes`：string / number / bool / null 四种类型各自压缩、以及混合类型（string "1" vs number 1）*不*合并的正确性
  - `Thresholds`：`MinCount` / `MinMatchRatio` / `MinNetSaveBytes` 三阈值独立 bail-out（含 `MinNetSaveBytes` 放大到 1000 的强制触发）
  - `ForcedDefault`：N=1 小样本也命中；非等值条目字段保留；`ForcedDefault + AutoDiscover` 联用（模拟 `get_output_log` 行为）
  - `AutoDiscover`：未声明字段自动发现；内置 13 个身份字段全量守护（`name/path/assetPath/nodeId/tag/message/timestamp/frame/id/label/title/text/error` 逐一断言不进 defaults）；业务追加排除字段
  - `EdgeCases`：空数组不 crash；字段缺失时不产 defaults；按 `HaveCount` 分母计算占比；`Emit` 空前缀 → `defaults`、非空 → `<prefix>_defaults`；`HasDefaults=false` 时 `Emit` 为 no-op
  - `AutoRecursive`：外层 `results_defaults` + 内层 `items_defaults` 双层递归；工具侧已写入 `<K>_defaults` 时 Pass 跳过（不双写不覆盖）；`content` 协议字段跳过；30 层深度病态对象链不崩；nullptr / 空对象安全
  - UE_4.26 BuildPlugin PASS / 零警告
- perf: `FNexusResponseCompactorUtils` 核心算法多项微优化（行为不变、纯加速）：
  - 消灭分布统计时的字符串 key 分配 —— 移除 `JsonValueKey(FString::Printf)` 热路径，改用 `AreScalarJsonValuesEqual` 对标量 FJsonValue 做类型化直接比较（string / number / bool / null 四种零分配）；抽取阶段也改走同一比较，每条 item 省掉 1 次 FString::Printf
  - 分布桶由 `TMap<FString, Pair>` 改为 `TArray<Pair, TInlineAllocator<8>>` —— 压缩目标字段基数在真实负载里通常 ≤ 5，栈上内联桶 + 线性扫比 FString hash 更快、还省一次堆分配
  - `SetAutoDiscover` 不再把内置 13 个身份字段（`name` / `path` / `id` 等）逐个塞进 per-instance `TSet<FString>`；改为进程级共享 `TSet` 单例，仅 per-instance 保留调用方追加的业务字段。自动 Pass 下每个对象数组创建 1 个 `FNexusResponseCompactorUtils` 实例，响应树里 N 个数组原本会重复分配 N×13 次 `FString`，现在降为 0
  - `AutoCompactRecursiveImpl` 不再在压缩后 `SetArrayField` 重新包一次 `FJsonValueArray` —— 直接对 `Value->AsArray()` 做 `const_cast` 就地操作（inner `FJsonObject` 的字段移除通过 `TSharedPtr` 天然可见，外层 `TArray` 结构根本未被修改）；每层数组省掉 1 次 `TArray` 拷贝 + 1 次 `MakeShared<FJsonValueArray>`
  - `CompactArray` 增加快速 bail：当 `ForcedDefaults` 为空且 `Items.Num() < MinCount` 时立即返回，省掉一整轮 `TryCompactField` 的 O(N×fields) 空扫。自动 Pass 下对小数组（pagination 后 count < 3 的场景）收益明显
  - 自动扫描收集字段时用 `Handled.Contains ∪ Discovered.Contains` 组合过滤替代"收集完再 diff"，省掉一次集合遍历
  - UE_4.26 BuildPlugin PASS / 零警告；所有变更保持输出字节级一致，不影响 `test_99_response_compact` 断言

- refactor: 清理所有工具侧**冗余的手动 `AddCandidate` + `SetAutoDiscover` + `CompactArray` + `Emit` 调用** —— 由于 `NexusMcpDispatcher::HandleToolCall` 的自动 Pass 对所有 `StructuredContent.对象数组` 字段默认启用了 `SetAutoDiscover(true)`，且各工具原有的 `AddCandidate(...)` 字段与自动扫描走的是同一阈值（`MinCount=3` / `MinMatchRatio=0.7` / `MinNetSaveBytes=30`），最终抽取结果完全一致 —— 这些手动声明纯属重复。本轮清理移除 9 个文件的 13 处手动块：`list_actors` / `list_blueprints` / `get_asset_refs` / `get_widget_tree` / `list_runtime_widgets` / `get_animation` / `get_gameplay_tags` / `get_behavior_tree` / `get_property`(components+attach_hierarchy 两处) / `get_asset`(graphs / nodes / defaults / properties / all 下的 graphs / UDS fields / Material params / MI params / expression nodes 共 7 处)。仅 `get_output_log` 保留 `AddForcedDefault("category", categoryFilter)` 分支（配合 `SetAutoDiscover` 顺带处理 `verbosity`），因为强制默认无需三阈值，能覆盖 `< MinCount` 的小样本响应，这是自动 Pass 做不到的。所有涉及文件同步移除 `#include "NexusResponseCompactorUtils.h"`；UE_4.26 BuildPlugin PASS=1/FAIL=0/WARNINGS=0。
- perf: 响应默认值压缩升级为**全工具默认启用** —— `FNexusResponseCompactorUtils` 新增静态方法 `AutoCompactRecursive(Parent)`，递归遍历任意 JSON 响应树，对其中所有"对象数组"字段 `K`（未出现同级 `<K>_defaults`、非 `content` / `*_defaults` 等协议名）启用 `SetAutoDiscover(true)` 尝试抽取，命中三阈值则同级写入 `<K>_defaults`。`NexusMcpDispatcher::HandleToolCall` 在每次工具执行成功后、序列化前对 `StructuredContent` 调用一次此 Pass，工具侧**不再需要手动 `AddCandidate` + `CompactArray` + `Emit`** 就能自动获得压缩能力；已存在的手动调用继续生效（检测到同级 `<K>_defaults` 则跳过避免双写），仅保留于需要 `AddForcedDefault` 的少数场景（如 `get_output_log` 的 `categoryFilter` 回显）。递归深度硬限 16，避免病态响应导致栈深；受 `bCompactResponseDefaults` 总开关控制
- perf: `FNexusResponseCompactorUtils` 新增自动扫描模式（`SetAutoDiscover`）——在显式 `AddCandidate` 基础上，自动遍历条目内所有标量字段，满足三阈值时一并抽取；内置 13 个身份字段排除集（`name/path/assetPath/nodeId/tag/message/timestamp/frame/id/label/title/text/error`），调用方可追加额外排除。重构提取私有 `TryCompactField` 消除 Candidates 与自动扫描路径的代码重复。全量 14 处调用点均开启 `SetAutoDiscover(true)`。`get_asset section=defaults` 新接入 `DefaultsCompactor`（候选 `inherited`），`inherited:true` 在纯继承属性 BP 里可整体省略。`test_99_response_compact.py` 补 2 条用例，`measure_response_tokens.py` 补 `section=defaults` 度量场景

- perf: 移除 `<prefix>_defaultsSchema` 冗余字段 —— AI 消费侧只需 `{**defaults, **entry}` 合并，无需 schema 键列表；`FNexusResponseCompactorUtils::Emit()` 不再输出 `_defaultsSchema` 数组，`Schema` 私有成员及 `GetSchema()` 接口一并删除；`measure_response_tokens.py` / `Tests/_framework/assertions.py` / `test_99_response_compact.py` 同步移除相关 pop 与 schema 对齐断言
- perf: `get_property section=components|attach_hierarchy|all` 接入 `FNexusResponseCompactorUtils` —— `components` 列表候选 `bIsActive` / `bHiddenInGame` / `bVisible` / `bRegistered`，`hierarchy` 列表候选 `attachParent` / `attachSocket` / `relativeRotation`；实测 BP_UGCGameCharacter（94 组件 / 22 层级节点）整体字节节省 ≈24%；`Script/measure_response_tokens.py` `_scenarios` 补 `get_property(actorName, section=all)` 度量场景

- perf: MCP 响应默认值压缩 —— 引入通用 `FNexusResponseCompactorUtils` 工具类，对列表/数组类返回体中主流值重复的字段做"抽取到顶部 `<prefix>_defaults`、条目内省略"的压缩，AI 消费侧以 `{**defaults, **entry}` 合并还原。起因：`.nexus-feedback` 反复出现 `list_actors` / `list_assets` / `get_asset(section="all")` 返回体中 `class` / `assetType` / `kind` / `graphType` 等字段在 N 条里 N 次重复占 token 的痛点；实测 `list_assets(Blueprint)` ≈18%、`get_asset(section=all)` ≈24%、`list_actors` ≈12%、`diff_actors(batch N≥3)` 15~45% 的 JSON 字节节省。落地：
  - `Public/Utils/NexusResponseCompactorUtils.h` + `Private/Utils/NexusResponseCompactor.cpp` 提供 `AddCandidate` / `AddForcedDefault` / `CompactArray` / `Emit` 四段式 API，三阈值（`MinCount=3` / `MinMatchRatio=0.7` / `MinNetSaveBytes=30`）兜底避免小响应上产生负收益；仅抽标量（string/number/bool/null），不触碰 object/array 嵌套
  - `UNexusLinkSettings` 新增 `bCompactResponseDefaults`（默认 ON，"服务器" 分类下的运行时开关；关闭后工具返回未压缩完整条目，便于排查 AI 解析异常或做字节级对比）
  - P0 接入：`list_actors` 候选 `class` / `location`（输出 `actors_defaults`）；`list_assets` 候选 `assetType` / `parentClass` / `rowStruct` / `parentMaterial`（输出 `assets_defaults`）
  - P1 接入：`get_asset` Blueprint handler 对 `properties` 数组候选 `kind` / `category` / `isPublic`（输出 `properties_defaults`，`section=variable/component/function` 过滤下命中率极高），对 `graphs` 数组候选 `graphType` / `disabledNodeCount`（输出 `graphs_defaults`，业务 BP 绝大多数为 `function` 且 disabled 为 0）；`section=all` 与 `section=graph`（无 graphName）两条路径共享同一压缩策略
  - P2 接入：`diff_actors` 批量模式（`actorNames=[baseline, ...]`）的 `valueA` 迁移 —— 基准 Actor 的某条 path 在所有 comparison 里恒等，集中到 `baseline.values: { path → valueA }` 字典，`comparisons[].diffs[]` 里不再出现 `valueA`，只保留 `{ path, type?, valueB }`。N 越大节省越可观（经测 `diff_actors(N=3)` ≈45%）
  - `docs/tool-reference.md` 顶部通用约定新增"响应默认值压缩"段说明合并规则与覆盖工具清单；`list_actors` / `list_assets` / `get_asset` / `diff_actors` 四段各追加一行标注自有的候选字段
  - L2 新增 `Tests/test_99_response_compact.py`（软断言：若产生 defaults 则 schema 键集合必须与 defaults 对齐，合并后每条必含必要字段；数据不足触发阈值时允许无 defaults）；`_framework/assertions.py` 新增 `merge_with_defaults` / `assert_respecting_defaults` 两个 helper
  - 新增 `Script/measure_response_tokens.py`：对 list_actors / list_assets / get_asset / diff_actors 发真实调用，本地反演压缩前原始条目，打印字节 + token 节省率报表（tiktoken 可选）
  - **扩展覆盖至 7 个新工具/handler**（P0+P1+P2 全量，所有标量字段扎堆的数组返回体）：
    - `get_asset_refs`：每个 `results[i]` 内嵌 `refs_defaults`（候选 `assetType`），同类资产扎堆引用时命中；配套 `_expand_nested_defaults` 反演嵌套在 entry 内的 defaults
    - `get_asset` BP/Material/MaterialFunction `section=graph` 指定 `graphName`：`nodes_defaults`（候选 `nodeClass`），长函数图表 `K2Node_CallFunction` / `K2Node_VariableGet` 扎堆时节省最显著
    - `get_asset` Material/MI `section=params`：`parameters_defaults`（候选 `paramType`，`scalar/vector/texture` 同类参数扎堆时命中）
    - `get_asset` UserDefinedStruct：`fields_defaults`（候选 `type`，同类型字段扎堆时命中）
    - `get_widget_tree`：`widgets_defaults`（候选 `class`，`VerticalBox` / `Image` / `TextBlock` 等同类控件扎堆时命中）
    - `list_runtime_widgets`：`widgets_defaults`（候选 `widgetClass` + `class`，同模板多实例或同类子控件扎堆时命中）
    - `get_output_log`：`entries_defaults`（候选 `category` + `verbosity`，`verbosity=log` 或 `categoryFilter` 收窄时命中）
    - `docs/tool-reference.md` 顶部通用约定段扩展为 10 处覆盖清单；7 段工具条目各追加一行候选字段标注
  - **进一步优化已有接入点 + 新增 3 个工具**（P0 ForcedDefault 提升 + P1/P2 新工具）：
    - `get_output_log` 优化：`categoryFilter` 有值时改用 `AddForcedDefault("category", categoryFilter)`，跳过统计阈值，小样本（< MinCount=3）也保证压缩命中；无 `categoryFilter` 时保持原统计路径
    - `get_gameplay_tags` `section=hierarchy` 新增：`tags_defaults` 候选 `childCount`，叶节点（`childCount=0`）占多数时命中，大型 Tag 树（200+ 条）可省 2-3 KB
    - `get_behavior_tree` `section=blackboard` 新增：`keys_defaults` 候选 `type`，同类型 BB Key 扎堆时命中
    - `docs/tool-reference.md` 通用约定段扩展至 13 处，3 个新工具段各追加压缩标注
    - `Tests/test_99_response_compact.py` 追加 7 个 soft-assert 用例（skip 兜底缺样本场景如 UDS 未创建、Material 不存在、非 PIE 等）；`Script/measure_response_tokens.py` `_scenarios` 并入 7 场景 + 新加 `_measure_asset_refs` 专走嵌套路径
  - **新增 `get_animation section=variables` 压缩接入**：每个 result 项内 `variables_defaults` 候选 `type` + `value`；全 `bool` / 全 `UAnimSequence*` 或大量 `None` 值的 AnimBP 可触发（PIE 实测复杂 ABP 因类型多样未触发，对单类型主导的 AnimBP 收益显著）
- refactor: 移除 dispatch 出口的 `_meta.shape` 响应尾注注入 —— 起因：每次工具调用返回的 `structuredContent._meta.shape = { topKeys, bytes, hint }` 尾注实际占用了可观 token（3 个字段 + topKeys 数组），用户观察下来它并没有稳定触发 AI 调 `report_unused_fields`，投入产出不划算。处理：
  - 删除 `NexusResponseShapeInjector.{h,cpp}` + `ShapeInjectorTests.cpp`；`NexusMcpDispatcher::HandleToolsCall` 的两遍序列化路径回退为单遍直出
  - `UNexusLinkSettings` 移除 `bInjectShapeHint` + `ShapeHintThresholdKB` 配置项（保留总开关 `bEnableFeedback`）
  - `report_unused_fields` 工具本身**保留**；触发规则替换为"AI 每次大响应后自检"三条式硬规则：① 响应 ≥ 5 KB ② 本轮实际引用的 top-level 键 ≤ 总键数 / 3 ③ 同会话同工具上报 < 2 次。同时满足即自动触发，替代原先依赖尾注的被动信号
  - `nexus-ai/rules/AI反馈规范.mdc §2` 改写为自检清单 + 典型场景（`get_asset(section="all")` 5 段只查 1 段）；`nexus-ai/rules/NexusMCP调用规范.mdc §8` 追加优化优先级第 4 条指向字段级反馈；`docs/tool-reference.md` 三条触发条件对齐；`nexus-unreal/README.md` 移除 `_meta.shape.topKeys` 引导字样

## [1.5.4] - 2026-04-23

- feat: 字段维度反馈闭环 —— 新增 `report_unused_fields` MCP 工具 + dispatch 出口对大响应自动注入 `structuredContent._meta.shape` 尾注。起因：`.nexus-feedback` 里 response_bloat 痛点只能靠 `submit_feedback` 走自由文本描述，粒度停在"整体冗余"，无法指明"具体是哪些 top-level 键被 AI 完全忽略"，后续定向瘦身缺数据依据；并且 AI 从未使用过的响应字段没有锚点，自由联想易臆造键名。落地：
  - `FNexusFeedbackCollector::RecordFieldUsage` 新增 JSONL 条目 `kind=usage` / `category=field_unused`（`tool` / `usedKeys` / `ignoredKeys` / `responseBytes` / `note`），与既有 `kind=auto/manual` 并列不互染
  - 新工具 `report_unused_fields`（schema 全英 token 极简：`relatedTool` / `usedKeys[]` / `ignoredKeys[]` / `responseBytes` / `note`），`submit_feedback.category` 枚举追加 `response_bloat` 覆盖"整体冗余但不值得列具体字段"情况
  - `FNexusResponseShapeInjector` dispatch 出口对正常响应（非 error/非反馈三工具）做两遍序列化：首遍统计原始字节，超过阈值（默认 5 KB）后注入 `_meta.shape = { topKeys, bytes, hint }`（英文 hint）供 AI 抄作业；同会话同工具 30 秒节流一次
  - `UNexusLinkSettings` 新增 `bInjectShapeHint` + `ShapeHintThresholdKB` 可调；`IsToolEnabled/SetToolEnabled` 对三个反馈工具特判（总开关决定可见性，`DisabledTools` 白名单不参与），`PostEditChangeProperty` 总开关变更时 `BroadcastToolsChanged` 热刷新客户端工具列表
  - `FNexusMcpToolDefinition::bHideInSettings` 框架字段 + `NexusLinkSettingsCustomization` 通用过滤：反馈三工具标记该 flag 后从设置面板 MCP 工具列表、"全选 / 仅只读" 快捷按钮通用消失（不依赖硬编码工具名），完全由主开关控制
  - 报告聚合：`FFeedbackStats` 追加 `ToolIgnoredKeyCount` / `ToolIgnoredBytesTotal` / `ToolFieldReportCount` / `FieldUsageCount`；`FNexusFeedbackReporter` 新增 `§5 字段冗余 Top 5` 段（按累计浪费字节降序取 Top 5 工具 + 每工具 Top 5 忽略键），后续段号顺延至 §9
  - `AI反馈规范.mdc` 激进瘦身 2758 → 1140 字符（-59%）：合并三工具触发点到硬规则表，`report_unused_fields` 明确条件绑定到响应尾注 `_meta.shape.hint`（看不到尾注 → 该规则不触发，避免 AI 臆造键名）；`NexusMCP调用规范.mdc §9` 同步追加新工具一行 + `_meta.shape` 约定指向
  - L1 覆盖 `FeedbackAggregatorTests.cpp`（`RecordFieldUsage` 落盘 / `usage` 条目不误算 auto·manual / `ToolIgnoredKeyCount` 累计 / 报告含"字段冗余"段）+ 新增 `ShapeInjectorTests.cpp`（阈值/节流/错误响应/反馈三工具/主开关关闭 五档断言）
  - L2 `test_10_editor.py` 追加 `submit_feedback` / `report_unused_fields` / `export_feedback` happy-path + 报告含 §4/§5 段断言
  - `docs/tool-reference.md` 新增 `report_unused_fields` 小节 + `_meta.shape` 尾注约定小节；根 README.md 工具总数 56 → 57

## [1.5.3] - 2026-04-23

- feat: `get_property` (actor target) `section` 新增 `"all"` 选项，一次返回 `components + hierarchy` 两段并附 `sections:["components","attach_hierarchy"]` 覆盖清单 —— 起因 `.nexus-feedback` 反馈 `get_property section="all"` 被拒（`Unknown section: 'all' (supported: components / attach_hierarchy)`）。根因 AI 按 `get_asset` / `NexusMCP调用规范.mdc §4` 里 "需多 section → `section="all"`" 的内化心智传 `"all"`，而 `get_property` 未对齐此惯例，AI 踩坑后需再发 2 次分别拉 `components` 和 `attach_hierarchy`，额外浪费 1 轮调用 + token。修复：
  - 新增 `WriteActorComponentsSection` / `WriteActorAttachHierarchySection` 两个文件级静态辅助，从原内联块抽出，`components` / `attach_hierarchy` / `all` 三分支复用
  - `NexusMcpToolGetProperty.cpp` 的 `section` 枚举追加 `"all"`；`IsSectionPreset`（用于 `diagnose` 错误的跨槽互查提示）同步识别 `"all"`
  - 错误文案升级为 `"Unknown section: '<X>' (supported: components / attach_hierarchy / all)"`
  - L2 pytest 新增 `test_actor_section_all` + `test_actor_section_invalid_hints_all` 两条：前者断言 `components`/`hierarchy`/`sections` 三字段齐全，后者断言错误消息含 `all` 提示
- fix: `NexusLink` 设置面板 "AI 反馈" 分类下 `打开目录` / `导出 Markdown` 按钮在 Windows 上无响应 —— 根因 `FNexusFeedbackCollector::GetFeedbackDir()` 返回 `FPaths::ProjectDir()` 拼接的相对路径 `../../../../Project/.nexus-feedback`（UE 项目目录默认相对于 `Engine/Binaries/Win64/`），`FPlatformProcess::ExploreFolder` / `LaunchFileInDefaultExternalApplication` 底层的 `ShellExecuteW` 基于调用进程当前工作目录解析相对路径，编辑器的 CWD 并非 `Engine/Binaries/Win64/`，Windows 资源管理器直接"空响应"（无错误、无窗口），导致点"打开目录"无反应、点"导出 Markdown" 报告文件成功落盘但不自动打开。`GetFeedbackDir()` 内部改走 `FPaths::ConvertRelativePathToFull` 先转绝对再返回，面板统计行显示的目录文本也随之变为完整路径（更易复制），按钮行为立即修复；`feedback.jsonl` / 归档 / 报告文件所有写入路径统一受益
- refactor: NexusLink 工具类按"目录归属"拉齐到 `Utils/` 子目录 —— 此前 11 个工具头/实现文件散在 `Public/` 和 `Private/` 根层与 `Tools/` 目录混排，目录归属不清晰；按 `Tools/` 已按领域分子目录的先例，把工具类全部搬进 `Public/Utils/` 和 `Private/Utils/`。涉及 `NexusAssetLoadUtils` / `NexusPinTypeUtils` / `NexusPortUtils` / `NexusPropertyUtils` / `NexusStringMatch` / `NexusVersionCompat` 共 6 类、11 文件（git mv 保留历史），裸文件名 include 写法全部保留、业务代码零改动。`NexusLink.Build.cs` 同步配置：
  - 新增 `PublicIncludePaths` 含 `ModuleDirectory/Public/Utils`（绝对路径；相对路径会被 UBT 当作 Engine/Source 相对解释而报 "Referenced directory does not exist" warning）
  - `PrivateIncludePaths` 追加 `NexusLink/Private/Utils`，与既有 `Private/Tools/*` 子目录并列
  - 跨版本编译验证：9 个 UE 版本（UE_4.26 / 4.27 / 5.0 / 5.1 / 5.3 / 5.4 / 5.5 / 5.6 / 5.7）全量 PASS，零警告

## [1.5.2] - 2026-04-22

- fix: 反馈埋点 `argsDigest` 数组字段补内容 hash，修 `repeat_pattern` 按长度误合并 —— 起因同批反馈报告里 2 条 `repeat_pattern` 痛点（`get_asset` 的两批资产在 30 秒内打 4 次调用），但细看原始 JSONL 发现这 4 次 `assetPaths` 内容并不完全相同（`BP_HeroBase`/`BP_Enemy` 与 `BP_Weapon`/`BP_Projectile` 两组分别 2 次），却因 digest 只记 `"<array len=2>"` 被聚合为同一 key，误判为"同调用重复"。根因：`FNexusFeedbackCollector::BuildArgsDigest` 对 `EJson::Array` 分支仅输出长度 placeholder，丢失内容差异。
  - 新增 `NexusFeedbackInternal::ShortArrayContentHash`：按元素类型（字符串/数字/布尔/对象/数组）稳定序列化后走 `FCrc::StrCrc32` 得 8 字符 hex；顺序敏感、空数组固定 `00000000`、跨进程/跨版本稳定
  - digest 数组分支输出从 `<array len=N>` 升级为 `<array len=N hash=XXXXXXXX>`，长度信息不丢失，同时让 `repeat_pattern` 的 digest key 能区分"同工具同长度但不同内容"的批次
  - L1 自动化测试：新增 `NexusLink.Utils.FeedbackDigest.ArrayHash` 覆盖同内容稳定、顺序敏感、空数组、字符串/数字/布尔/对象混合类型差异 5 档断言
  - 导出：`ShortArrayContentHash` 声明补 `NEXUSLINK_API`，供 `NexusLinkTests` 模块链接

- fix: 全线批量工具统一"单/复数运行时容错"策略 —— 起因 `.nexus-feedback` 单次报告 8 条埋点里 5 条 tool_error 指向 `assetPaths array is required`，根因是 1.5.0 破坏性清理后 `get_asset` 系列强制只收复数数组，但 AI（尤其新会话 / 多模型）沿用同项目 `get_property` (asset target) / `spawn_actor` 的单复数兼容心智仍频繁传 `assetPath` 单数，报错后重试推高轮次。修复分三层：
  1. **手写批量工具（4 个）**：`get_asset` / `get_asset_refs` / `save_asset` / `compile_blueprint` 在参数解析阶段加 `assetPath` → `assetPaths` 单数 fallback，误传单数时包装成单元素数组继续执行
  2. **手写批量工具（1 个同工具内部不一致）**：`get_property` (widget target) 原只收 `widgetNames`，而同工具 actor/asset target 都支持单/复数，修复 `widgetName` 单数 fallback 对齐。错误文案从 `widgetNames is required` 统一为 `widgetName or widgetNames is required`
  3. **基类 `FNexusBatchMcpTool`（覆盖 17 个子类）**：新增 `GetSingularKey()` 虚函数，默认按"去尾 `s`"推导（`actorNames` → `actorName`、`variables` → `variable`、`updates` → `update`），数组缺失/空时若单数键存在则按类型（字符串/对象）包装成单元素数组继续执行。子类可覆写返回空字符串关闭 fallback。覆盖范围：`destroy_actor` / `get_animation` / `manage_animation` / `data_table_row` (get) / `manage_blueprint_variable` / `manage_blueprint_component` / `manage_blueprint_graph` / `manage_blueprint_wires` / `manage_struct_field` / `manage_widget` / `manage_material` / `manage_material_wires` / `manage_data_table_row` / `interact_widget` / `spawn_actor` / `set_property` 等
  - **Schema 不变**：`required` 数组与参数 description 仍只宣传复数形态，保持"批量化"对外宣传不动摇；fallback 纯运行时保底，不鼓励 AI 依赖单数形态做新功能设计
  - **零破坏**：复数数组非空时走原路径完全不变，仅在缺失/空时触发单数探测
  - 规范 `NexusUnreal规范.mdc` §"单/复数运行时容错" 与 `docs/tool-reference.md` 通用约定段同步更新，明确三档容错位置和基类推导规则

## [1.5.1] - 2026-04-21

- fix: `Script/check_tool_tokens.py` 参数识别正则不认命名空间前缀 —— 原正则 `(?:Make\w+|Str|Int|Bool|Num|Enum|StrArr|ArrayOf|AnyObject)\s*\(\s*` 只匹配裸标识符，`get_property` 的 3 个 `NexusSchema::Enum(...)` 参数全部漏计，报 215 tokens（真实 269）。新增可选前缀 `(?:\w+\s*::\s*)?` 覆盖任意命名空间限定。验证：aggregate `~5671 → ~5745`（+74），`get_property` 215 → 269
- fix: `capture_viewport` token 压缩至 200 预算内 —— Def.Description 去除和 `target` 参数 desc 的冗余枚举列表（`"Screenshot editor window / viewport / PIE / panel / Actor bbox / UMG widget."` → `"Screenshot editor UI / viewport / PIE / Actor / Widget."`），`ownerClass` 去掉赘述的 `"for widgetName"` 后缀，`maxSize` / `viewAngle` / `windowIndex` / `widgetName` desc 小幅精简。结果：212 → 197 tokens（9 参数），软违规从 6 条降到 5 条
- chore: `.cursor/rules/NexusUnreal规范.mdc` 新增 §"200 tokens 软预算豁免清单"章节，显式钉住 5 个结构性无解工具的豁免理由：`get_asset`(285 / 13 参，合并器)、`manage_blueprint_graph`(272 / 14 参，K2Node 子类构造必需)、`get_property`(269 / 12 参，三 target × 单复数批量)、`manage_material`(250 / 12 参，字段语义分化待 major BC)、`get_lua`(240 / 11 参，8 section 共用参数空间)。同步 `Script/check_tool_tokens.py` 引入 `TOOL_TOTAL_EXEMPT` 集合 + `Violation.exempt` 字段 + `--strict` 仅对非豁免违规阻断；报告输出新增 `[EXEMPT]` 标记。结果：`check_tool_tokens.py --strict` 在 5 条豁免软警告下退出码 0，`release-version` skill §0.4 token 门槛正式可用
- fix: `get_property` / `set_property` 的 `target` / `section` / `diagnose` 三条参数改走 `NexusSchema::Enum` 原生 enum 字段，把 `'actor' | 'widget' | 'asset'` 等枚举值从 description 字符串里剥离；两个工具的 `Def.Description` 同步收敛到 ≤ 80 字符。消除规范 `.cursor/rules/NexusUnreal规范.mdc` 下 5 条硬违规（2 条 Def.Description > 80 chars + 3 条 param description > 50 chars 且混写枚举），与 `capture_viewport` 等已迁移到 `NexusSchema` 的工具对齐
- fix: `Script/check_tool_tokens.py` 的 `parse_instructions()` 原只扫 `NexusMcpDispatcher.cpp` 里的 `SetStringField(TEXT("instructions"), TEXT(...))` 字面量，但 instructions 文本早已挪到 `Plugin/Resources/InitializeInstructions.md` 由 `LoadInitializeInstructions()` 运行时加载，导致 linter 报 `instructions tokens : ~0` 假阴性；新增 `INSTRUCTIONS_MD` 模块级常量，优先读 MD 文件、找不到再回落旧版 C++ 字面量提取。验证：`py Script/check_tool_tokens.py --strict` → instructions tokens 从假 0 恢复到真实 ~365（≤ 800 预算），5 条硬违规清零，仅剩 6 条工具总 tokens > 200 软违规
- fix: `Script/build_test.py` 和 `Tests/_framework/ue_launcher.py` VS2017 fallback 集合扩展到 UE_4.27 —— 原只对 UE_4.26 自动追加 `-VS2019`，UE_4.27 的 UBT 同样默认查找 VS2017，在仅装 VS2019/VS2022 的现代机器上会报 `ERROR: Visual Studio 2017 must be installed`。现以 `_VS2019_FALLBACK_VERSIONS = frozenset({"UE_4.26", "UE_4.27"})` 模块级常量承载，build_test.py + ue_launcher.py 两端共用，未显式传 `--vs` 时对集合内版本自动回落到 VS2019；`--vs` / `--help` 说明同步更新。验证：`py Script/build_test.py --versions UE_4.27` → PASS=1 / FAIL=0 / WARNINGS=0

## [1.5.0] - 2026-04-21

- feat: pytest auto-launch **默认改为 GUI 模式**（`UnrealEditor.exe` / `UE4Editor.exe` 带 Splash + 主编辑器窗口），此前默认 `UnrealEditor-Cmd -nullrhi -unattended` 看不到过程难以排障。`UELauncher` 新增 `headless: bool = False` 字段 + `resolve_editor_exe(ue_root, headless=...)` 切换 binary；`conftest.py` 新增 `--headless` pytest option；`run_e2e.py` 新增 `--headless` 开关透传（CI / 低开销场景启用）。GUI 模式仍带 `-stdout -FullStdOutLogOutput -skipcompile` 把 engine log 镜像到 `Saved/Logs/UE-auto-launch.stdout.log`，早退诊断链路不变
- chore: pytest 补齐 5 个此前未被 L2 覆盖的工具——`call_blueprint_function`（`test_95_pie_runtime`）、`get_animation` / `manage_animation`（`test_80_ai_anim_assets`）、`set_data_table_row`（`test_20_struct_datatable`）、`get_slate_widget`（`test_00_smoke`）。全部走 **真实执行链 + 错误路径**：传不存在的 actor/function/row/field/hex 地址，验证工具不是 top-level 抛出而是返回批量契约 `{totalCount, failCount, results:[{error}]}`；Slate Widget 允许 `MCPError` 或 payload 带 error 字段两种拒绝形态。5 条用例全绿 → 67 passed / 0 fail / 0 skip
- feat: `Script/run_e2e.py` **零参 auto 模式**全托管 UE Editor 生命周期——自动扫 `127.0.0.1:45000..45009` 复用 live Editor；没有则按 `Nexus.uproject` 的 `EngineAssociation` 匹配 UE 版本（避免盲挑最新版缺 UnLua）、headless 拉起 `UEEditor-Cmd -unattended -skipcompile`、pytest 结束后自动关停。新增 `--no-launch` 关闭自动拉起（CI 硬失败场景用）。实测零参全流程 UE_4.26 冷启动 + 62 用例 pytest ≈ 47s 完成
- feat: `Tests/_framework/ue_launcher.py` 启动前自动 **UBT 预编译 Editor target**——`UEEditor-Cmd -unattended` 遇到 Nexus module DLL 缺失时只会 silent exit 1（不 prompt），现实现会先检查 `Binaries/Win64/*Editor-Nexus.dll`，缺失时调 `Engine/Build/BatchFiles/Build.bat <Target>Editor Win64 Development -Project=<uproject>`；target 名从 uproject `Modules[0].Name` + `Editor` 自动推断，UE 版本标签从 `ue_root` 目录名或 `Build.version` 解析，**UE_4.26 自动追加 `-VS2019`** 对齐 `build_test.py` 的 VS 回落策略
- chore: `Tests/_framework/ue_launcher.py` 的 `UEEditor-Cmd` 子进程 stdout/stderr 改为落盘到 `Saved/Logs/UE-auto-launch.stdout.log`（原先走 `DEVNULL`），启动早退时把日志末尾 40 行拼到 `RuntimeError` 里；UBT 预编译 stdout 同落 `Saved/Logs/UE-auto-launch-build.log`
- fix: `Tests/test_95_pie_runtime.py::test_actor_property_multi_actor` 断言对齐多 actor 返回结构——老实现返回扁平 `{results:[...]}`，现工具批量模式返回 `{actors:[{actorName, actorClass, results:[...]}, ...]}`，新断言同时兼容两种形态
- chore: `Script/build_test.py::_parse_automation_output` 扩展匹配模式兼容 UE5 输出 —— 原实现只识别 `Result={Passed}/{Failed}`，UE_5.0~5.7 跑 Automation 时全部 SKIP (`no Automation Result lines found in stdout`)；现支持 `Result={Passed}`/`Result='Passed'`/`Result=Passed` 及 `Success`/`Fail`/`Error`/`Skipped`/`NotRun` 变体，并在 per-test 行全无时回退扫描 `Tests Passed: N` / `Tests Failed: N` 汇总行。同时在 `_run_automation_once` 新增完整 stdout 落盘到 `Saved/Logs/Automation-<ver>.stdout.log`，便于下次 triage 直接 diff 实际格式（UE_5.x SKIP 真实根因待关闭 Editor 避免 45000 端口冲突后重跑采样确认）
- chore: `Script/build_test.py` 新增 `--vs {2017|2019|2022}` 开关透传 `-VS<ver>` 到 UAT `BuildPlugin`；未显式指定时 **UE_4.26 自动回落到 `-VS2019`**（UE 4.26 UBT 默认查找 VS2017，在仅装 VS2019/VS2022 的现代机器上会报 `ERROR: Visual Studio 2017 must be installed`）。验证：`py Script/build_test.py --versions UE_4.26 UE_5.1` → PASS=2 / WARN=0
- feat: `get_property` 的 `propertyPath` 支持 UFUNCTION 调用语法 `Name()` —— 尾段以 `()` 结尾时走 `UObject::FindFunction` + `ProcessEvent` 路径，仅放行 `FUNC_BlueprintPure | FUNC_Const` 且零入参的函数，返回值按叶子属性序列化；非 Pure/Const 或有入参均以明确错误拒绝。典型用法：`CharacterMovement.IsFalling()` / `GetVelocity()`（后者若为 Pure 返回 FVector 则直出完整结构体）
- fix: `get_property` 属性未命中时错误消息跨槽区分 UFUNCTION / 私有字段 / 真不存在 —— 原实现一律报 `Property 'X' not found on Y`，AI 无法辨别是打错字、字段没有 `UPROPERTY` 反射还是应该用 `X()` 调函数。现三分支：1) `FindFunction` 命中 → `"'X' is a UFUNCTION on Y; append '()' to invoke it"`（若非 Pure/Const 再补提示 get_property 仍会拒绝）；2) 都不命中 → `"Property 'X' not found on Y (not a UPROPERTY; private C++ fields without UPROPERTY are not reflected)"` 明确告知私有字段不可见
- feat: `diff_actors` 支持批量模式 `actorNames=[baseline, ...others]` —— 以首项为基准，后续逐一两两对比，返回体 `{baseline, comparisons:[{actorName, info, diffs, diffCount, truncated?} | {actorName, error}], totalCount}`；老 `actorNameA` / `actorNameB` 对模式完全向后兼容不变。典型用法：DS + Client1 + Client2 三实例同名 Actor 一次对比：`diff_actors(actorNames=[BP_Hero_C_0_DS, BP_Hero_C_0_C1, BP_Hero_C_0_C2], propertyPaths=["ActorScale3D","GetActorLocation()"])`
- fix: `manage_blueprint_component` 在 PIE 运行中默认拒绝 —— PIE 运行期间修改 SCS（加/删组件）会触发 `CompileBlueprint` → `ReinstanceObjects`，UnLua 的 `FObjectPropertyDesc` 反射缓存仍指向老 `UClass`，Lua 按错误 offset 读字段导致 `EXCEPTION_ACCESS_VIOLATION`。现 `OnBegin` 检测 `GEditor->PlayWorld != nullptr` 时直接返回错误，要求 caller 停 PIE 或显式传 `forceInPIE=true` 授权；`forceInPIE=true` 路径成功后在返回体追加 `note: "SCS changed while PIE is running; Lua bindings may be stale, consider restarting PIE"` 提示 AI/用户重启 PIE 刷新绑定
- fix: `get_property` (actor target) 的 `section` / `diagnose` 两个预设槽错误提示跨槽互查 —— 原错误只报本槽合法枚举，AI 把 `attach_hierarchy`（属 section）传给 `diagnose` 只能看到 `Unknown diagnose preset`，靠试错才能对齐。现在跨槽命中时追加 `" (did you mean section='attach_hierarchy'?)"` / `" (did you mean diagnose='visibility'?)"`
- fix: `manage_lua action=eval` 裸 Lua 全局下自动预置 `UE.*` 命名空间 —— 原实现在 UnLua 主 state 尚未 `require "UnLua"` 时全局 `UE` 为 nil，AI 直接写 `UE.UGameplayStatics.GetPlayerCharacter(...)` 报 `attempt to index a nil value (global 'UE')`。现 `ExecEval` 在 load 用户代码前先执行 best-effort bootstrap：`if rawget(_G,'UE')==nil then local ok,M=pcall(require,'UnLua'); if ok and M.UE then _G.UE=M.UE end end`，失败静默跳过不影响用户代码
- chore: 导出三个被 `NexusLinkTests` 引用的跨模块符号（`NexusStringMatch::Matches` / `FNexusFeedbackAggregator::Aggregate` / `FFeedbackStats::GetTopPain`）补 `NEXUSLINK_API`；`PinTypeUtilsTests.cpp` 的 `PC_Real` 用 `NX_UE_AT_LEAST(5,0)` 守卫（4.26 下该符号不存在）——解决 L1 Automation 模块链接失败
- **feat: 测试框架 1.0 —— L1 C++ Automation + L2 pytest E2E 双层自动化**
  - **L1（`Plugins/Developer/NexusLink/Source/NexusLinkTests/`）**：新增 `NexusLinkTests` 子模块（`Type=DeveloperTool`，Editor 构建下编译，不进发行产物），内含 5 个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`：`NexusLink.Utils.PinType.ParsePrimitives` / `NexusLink.Utils.StringMatch.Matches` / `NexusLink.Utils.AssetUtils.FindClass` / `NexusLink.Utils.FeedbackAggregator.Aggregate` / `NexusLink.Smoke.PluginAndRegistry`。跑法：`UEEditor-Cmd <uproject> -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi`
  - **L2（`nexus-unreal/Tests/`）**：新建 pytest 骨架 —— `conftest.py` 支持 `--ue-url`（连现成 Editor）与 `--ue-root/--uproject`（runner 自动拉 UEEditor-Cmd）双模式；`_framework/mcp_client.py` 薄 HTTP 封装（initialize 握手 + `Mcp-Session-Id` 自动续期 + `tools/call` 错误透传）；`_framework/ue_launcher.py` 基于 `psutil` 做父子进程清理；`test_ns` session fixture 自动把所有写入落到 `/Game/_McpTest/<ts>/` 并 teardown 清理
  - 11 个 `test_*.py` 按原 `TEST_CHECKLIST.md` 十二阶段 1:1 迁移；关键批量用例（3.2 Struct 批量、4.2/4.7/4.11 BP 批量、5.2 Widget 树、6.9 Material 两步法、11.7 批量 spawn、11.28 批量 interact_widget）全部自动化；Lua/PIE 用例按运行时可用性 `pytest.skip` 降级，不阻塞主流程
  - `Script/build_test.py` 新增 `--run-automation` 开关，BuildPlugin 通过后逐版本调 UEEditor-Cmd 跑 L1 并解析 `Result={Passed}/{Failed}` 统计；`build_test.bat` 默认开启（`--no-automation` 可关）；失败版本数计入进程退出码
  - `Script/run_e2e.py` 一键 pytest 封装，默认写 JUnit XML 到 `Saved/Logs/TestReport.xml`；`--no-pie` / `--no-lua` / `-k`/`-m` 等常用 flag 有快捷选项
  - `TEST_CHECKLIST.md` 从"人工勾选清单"退化为"TestId 映射索引表"，失去主事实源地位；`.cursor/skills/mcp-regression-test/SKILL.md` 重写为"跑 pytest 读报告"导向，禁止 AI 手工逐条调 `tools/call` 复刻 pytest 已覆盖的用例
- **BREAKING** refactor: 框架大重构——批量工具基类化 / 属性工具合并 / Dispatcher 解耦 / Feedback 分层，共 ~13 项结构性优化。
  - **工具合并（6 → 2，破坏性）**：`get_actor_property` / `set_actor_property` / `get_runtime_widget_property` / `set_runtime_widget_property` / `get_asset_property` / `set_asset_property` 全部合并为统一的 `get_property` / `set_property`。通过 `target` 参数（`actor` / `widget` / `asset`）或自动推断（`actorName/actorNames` → actor，`widgetNames` → widget，`assetPath/assetPaths` → asset）切目标；所有原有子参数保留（`section` / `diagnose` / `ownerClass` / `widgetName` / `propertyPath(s)` / `updates[]`），行为一一对应迁移。迁移示例：`set_actor_property(updates=[{actorName,propertyPath,value}])` → `set_property(target="actor", updates=[...])`（或省略 target 由入参推断）
  - feat: 新增批量工具基类 `FNexusBatchMcpTool`（`Public/NexusBatchMcpTool.h`），封装 `OnBegin` / `ApplyOne` / `OnEnd` + 统一数组参数解析 + 成功/失败计数 + 返回体 `results[]`/`totalCount`/`successCount`/`failCount?` 填充；Blueprint/Struct/Widget/DataAsset/Material/Runtime 所有批量工具（约 15 个）全部迁移到新基类，重复样板代码 -60%
  - refactor: 新增 `NexusAssetLoadUtils`（`LoadAssetWithFallback` / `FindClassWithUPrefix` / `LoadWidgetBP` / `FindWidgetByName`）和 `NexusPinTypeUtils`（`ParsePinType`），集中散落在各批量工具里的资产加载与 Pin 类型解析逻辑
  - refactor: `FNexusMcpToolResult` 去 `Content` 字段，改为 `StructuredContent`（结构化 JSON）+ `ErrorText`（错误文本）+ `OutputText`（可选预渲染文本，如 Markdown 报告）；Dispatcher 统一 JSON 序列化入口，所有工具不再手写 `SerializeObject`
  - refactor: 新增 `INexusDispatchObserver` 接口 + `NexusDispatchObservers` 线程安全注册表；`FNexusMcpDispatcher` 不再直接调 `FNexusFeedbackCollector`，改为通知所有 observer（`FNexusFeedbackCollector::Get()` 在 `StartupModule` 主动触发并自注册为 observer）
  - refactor: `FNexusFeedbackCollector` 拆分三层：
    - `NexusFeedbackInternal`（`.h/.cpp`）— 字符串工具 / 错误指纹 / 百分位 / 净 Role 检测 / 插件版本 / 条目读取
    - `FNexusFeedbackAggregator` + `FFeedbackStats`（`NexusFeedbackAggregator.h/.cpp`）— 数据聚合，产出纯结构体
    - `FNexusFeedbackReporter` + `IFeedbackSection`（`NexusFeedbackReporter.h/.cpp`）— 11 个段类独立渲染 Markdown，`Export` 只负责编排
  - refactor: `initialize.instructions` 从 C++ 硬编码搬到 `Plugins/Developer/NexusLink/Resources/InitializeInstructions.md`，插件加载时读取；大段文本脱离代码便于维护
  - refactor: `tools/list` 返回体去除 `description` 字段（AI 已依靠 instructions 与参数 schema 决策），token 占用进一步下降
  - 迁移提示：调用端 `get_actor_property` / `set_actor_property` / `get_runtime_widget_property` / `set_runtime_widget_property` / `get_asset_property` / `set_asset_property` 全部需要改名到 `get_property` / `set_property`；参数结构不变（原来传什么仍传什么），仅工具名变更
- feat: AI 反馈报告三件套（AI 建议速览 + 性能摘要 + 自动样本按分类拆分）——
  - **`## 3. AI 建议速览`**（新增）：按 `relatedTool`（兜底 `tool`）把 manual 条目的 `aiSuggestion` 聚合出"首句 → 出现次数"；工具行按总建议条数降序，每工具最多展示 Top 2 建议首句；首句提取 = 截至第一个 `。`/`.`/换行，最长 120 字；格式 `- tool（manual×N）→ "lead1" (×a) · "lead2" (×b)`
  - **`## 6. 性能摘要（slow/oversize 聚合）`**（新增）：仅汇聚 `category=slow` 或 `oversize` 样本的 `durationMs`/`responseBytes`；每工具算 p50/p95（`NexusFeedbackInternal::Percentile` 在升序数组上取 `N*pct` 位）+ 最大响应体积；表格 `工具 / 样本数 / p50 (ms) / p95 (ms) / 最大响应 (KB)`，按 p95 降序 Top 10
  - **`## 7/8. 自动埋点样本`**：原"平铺 20 条"拆为 4 个子节 `### 错误(tool_error)`/`### 慢(slow)`/`### 超大(oversize)`/`### 重复(repeat_pattern)`，每节最多 5 条；不同维护者的关心点（错误 vs 性能）分开展示，查找效率提升
  - **段号重排**：`## 3 错误指纹 → ## 5`、`## 4 AI 显式上报`（位置不变）、`## 5 明细分组 → ## 7`、`## 5/6 自动埋点样本 → ## 7/8`；Top 3 优先建议尾注 & Issue 预填模板尾注同步补上"AI 建议速览"与"性能摘要"
  - C++ 新增辅助：`NexusFeedbackInternal::ExtractLead`（兼容 `。`/`.`/换行，UE 4.26 下用 `static_cast<TCHAR>(0x3002)` 避免 `TEXT(0x3002)` 宏展开为 `L0x3002` 报 `C2065`）、`NexusFeedbackInternal::Percentile`（升序 Pct 位）；并把 UE 4.26 不支持的 `TArray::Add({k,v})` 大括号聚合初始化显式化为 `TPair<K,V>(k,v)` 构造
- fix: Issue 入口 URL 修正 —— 反馈报告 `## 复制到 Issue（预填模板）` 段的入口 URL 改为可配置的项目 Issue 页；相关段标题与文档描述统一去掉平台限定词，改为「项目 Issue」（影响 `NexusFeedbackCollector.cpp` 报告正文 + `nexus-unreal/README.md` + `docs/tool-reference.md` + `docs/usage-guide.md`）
- feat: AI 反馈报告新增"错误指纹 Top 10（去重聚合）"段 ——
  - 报告新增 `## 3. 错误指纹 Top 10（去重聚合）`，把随机变化的片段（引号字符串/UE 资源路径/数字序列）归一化为 `<str>`/`<path>`/`<n>` 占位符后分组计数，让"Actor X not found"与"Actor Y not found"合并成同一条目
  - 指纹算法 `NexusFeedbackInternal::BuildErrorFingerprint`：O(n) 单遍扫描，规则顺序 引号→路径→数字，空白折叠、首尾修剪、80 字符截断（超长末尾加 `…`）
  - 语料来源：auto 样本的 `errorMessage` + manual 样本的 `aiSummary`，两路统一入库，工具集去重存 `TSet<FString>`，例句存 200 字符截断原文
  - 表格列 `次数 / 涉及工具 / 指纹 + 代表消息`，Top 10 截断，算法说明以引用块置于表下方
  - 后续段编号顺移：`## 3 AI 显式上报 → ## 4`、`## 4/5 自动埋点样本 → ## 5/6`、`## 4 明细分组 → ## 5`；Top 3 优先建议提示文案同步补上 `错误指纹`；Issue 预填模板尾注同步列入"错误指纹 Top 10"
- feat: AI 反馈报告内容三件套（环境指纹 + 严重度评级 + Issue 预填模板）——
  - **环境指纹**：报告头固定加 `- 环境: UE <Engine> · NexusLink <Plugin> · <Platform> · <NetRole>` + `- 项目: <Name>` 两行，Issue 收件人一眼判位；来源 `FEngineVersion::Current()` + `IPluginManager::FindPlugin("NexusLink")` + `FPlatformProperties::IniPlatformName()` + `DetectCurrentNetRole()`（镜像 NexusMcpServer 实现，避免跨 TU 暴露 static）
  - **Top 痛点工具表增强**：原 2 列升级为 6 列 `工具 / 命中次数 / 严重度 / 错误 / 重复 / 慢或大`；严重度分 🔴 Critical（错误≥5）/ 🟠 High（重复≥3 或 错误≥2）/ 🟡 Medium（有错误/慢或大≥2）/ ⚪ Low（单次），由新增的 `ToolCategoryCount` 子计数表驱动；规则说明置于表格下方引用块
  - **Issue 预填模板**：报告末尾新增 `## 复制到 Issue（预填模板）` 段落，用 Markdown code block 生成即开即用的 Issue 草稿，自动填好 环境指纹 / 时间窗 / Top 1 涉及工具 + 严重度 + 命中数 / 代表错误 / AI 描述，用户只需补 `**复现步骤**` 即可提交；内含项目 Issue 入口 URL（可配置）
  - Build.cs 新增 `Projects` 模块依赖（`IPluginManager` 所属）
- feat: AI 反馈闭环 v2（A+B+C 三件套）——
  - **A**：设置面板 `AI 反馈` 分类面板化：
    - 四个阈值 UPROPERTY（`FeedbackSlowMs` / `FeedbackOversizeKB` / `FeedbackRepeatThreshold` / `FeedbackRepeatWindowSeconds`）全部热切换，`FNexusFeedbackCollector::RecordCall` 每次调用时读取最新值，告别硬编码 `constexpr`
    - 控制台行：实时显示 `当前 N 条 / X KB · 归档 K 个 · 目录: <path>`
    - 快捷按钮：`打开目录`（`FPlatformProcess::ExploreFolder`）/ `导出 Markdown`（等价 `export_feedback` 落盘 + 自动打开报告）/ `归档并清空`（当前 jsonl 改名为 `feedback-manual-<ts>.jsonl` 归档，不直接删）
  - **B**：`submit_feedback` 返回体补上下文 —— 新增 `pendingCount` / `pendingSizeBytes` / `shouldExport`；当条目 ≥ 20 条或 ≥ 1 MB 时，`Content` 末尾自动追加一行 Hint 让 AI 征询用户是否运行 `export_feedback` 汇总
  - **C**：`export_feedback` 可视化增强 —— 新增 `groupBy=tool|category|hour` 参数（`tool` 默认保持原行为，`category` 按分类×工具交叉表，`hour` 按小时×分类时间线）；报告头增加 **Top 痛点加权分趋势行**（`tool_error×3 + repeat_pattern×2 + slow/oversize×1 + manual×2`）；报告末尾新增"建议优先反馈 Top 3"段落，自动挑最值得贴 Issue 的 3 个工具及代表条目
  - 配套 Collector 公共接口：`GetPendingCount()` / `GetPendingSizeBytes()` / `GetArchiveCount()` / `ArchiveAndReset()`；`Export()` 签名扩展可选 `GroupBy` 参数（默认空串即旧行为，源码级兼容）
- docs: `nexus-unreal/TEST_CHECKLIST.md` 对齐第 1/2/3 批批量化升级 —— 全部单数参数调用改写为 `variables[]` / `components[]` / `fields[]` / `widgets[]` / `rows[]` / `nodes[]` / `wires[]` / `operations[]` / `updates[]` / `calls[]` / `spawns[]` / `assetPaths[]` / `actorNames[]` / `widgetNames[]` / `rowNames[]` 等批量形态；新增 7 条关键用例（3.2 一次批量建完整 Struct 字段、5.2 一次批量建完整 Widget 树、6.9 Material 两步法 add_node+set_node+recompile、6.10/6.11 Material 节点级反射穿透 `<nodeId>.ParameterName`、10.4 多资产并发 `get_asset(assetPaths=[...])`、11.7 批量 `spawn_actor`、11.19a 容器下标穿透 `RootComponent.AttachChildren[0]`、11.19b `call_blueprint_function(calls=[...])` 批量、11.28 单次 6 控件 `interact_widget` 批量、11.53 批量 `destroy_actor`）；项数从 160 → 145，结果列置为 ⏳ 待重测

- feat: AI 反馈采集开关新增设置面板入口 —— `UNexusLinkSettings` 新增 `bEnableFeedback`（Category=AI 反馈，默认启用），位于 `Edit → Editor Preferences → Plugins → NexusLink → AI 反馈 → 启用反馈采集`，取消勾选后 `FNexusFeedbackCollector` 自动/手动写入全部丢弃，编辑器内立即生效无需重启；原 `[NexusFeedback] bDisable=true` 保留为硬开关（冷关闭，优先级最高，适用于打包/Dedicated Server）；`submit_feedback` 被禁用时返回消息同步更新为同时提示两种关闭路径
- **BREAKING**(subtle): 第 3 批 10 个批量工具的 `results[]` 子项去掉冗余 `success: true` —— 对齐 `NexusUnreal规范.mdc §JSON 响应精简 / §批量返回体约定` 的"成功项省略 `success:true`、失败项必写 `error`"硬性要求，调用方请以 `error` 字段存在与否判定单项结果（不再依赖 `success`）。涉及：`manage_blueprint_variable / manage_blueprint_component / manage_blueprint_graph / manage_blueprint_wires / manage_struct_field / manage_widget / manage_data_table_row / manage_material / manage_material_wires / spawn_actor`

- **BREAKING**: 批量参数统一升级 第 3 批 —— 13 个写入/生成类工具彻底批量化，消除最后的"单对象操作"反模式（发版时对应 major 升级）：

  **API 残留清理（3 个工具，原支持 `assetPath` / `assetPaths` 二选一，现强制 `assetPaths[]`）**：
  - `save_asset`：`assetPath` 移除 → `assetPaths: string[]`（必填）
  - `compile_blueprint`：`assetPath` 移除 → `assetPaths: string[]`（必填）
  - `get_asset`：`assetPath` 移除 → `assetPaths: string[]`（必填）

  **A 类 · 高 ROI 批量化（6 个工具，单资产 + 多操作）**：
  - `manage_blueprint_variable`：顶层保留 `assetPath`，单项参数 → `variables:[{action, variableName, variableType?, defaultValue?, category?, isPublic?}]`（批量结束后统一重编译一次）
  - `manage_blueprint_component`：顶层保留 `assetPath`，单项参数 → `components:[{action, variableName, componentClass?, parentComponent?}]`（批量结束后统一重编译一次）
  - `manage_struct_field`：顶层保留 `assetPath`，单项参数 → `fields:[{action, fieldName, fieldType?, defaultValue?, newName?, newType?}]`（批量结束后统一结构编译+标脏一次）
  - `manage_widget`：顶层保留 `assetPath`，单项参数 → `widgets:[{action, widgetClass?, widgetName?, parentWidget?}]`（批量结束后统一标脏一次）
  - `manage_data_table_row`：顶层保留 `assetPath`，单项参数 → `rows:[{action, rowName, fields?}]`（批量结束后统一标脏一次）
  - `spawn_actor`：全字段 → `spawns:[{blueprintPath?, className?, locationX?, locationY?, locationZ?, rotationPitch?, rotationYaw?, rotationRoll?}]`（共享当前 World，一次调用生成多个 Actor）

  **B 类 · 中 ROI 批量化（4 个工具，单资产 + 多图/节点操作）**：
  - `manage_blueprint_graph`：顶层保留 `assetPath` + `graphName`，单项参数 → `nodes:[{action, nodeId?, nodeClass?, functionName?, functionClass?, variableName?, posX?, posY?, comment?, pinName?, pinDefaultValue?}]`
  - `manage_blueprint_wires`：顶层保留 `assetPath` + `graphName`，单项参数 → `wires:[{action, sourceNodeId, sourcePinName, targetNodeId?, targetPinName?}]`
  - `manage_material`：顶层保留 `assetPath`，`action` 及全部字段 → `operations:[{action, paramName?, paramType?, value?, expressionClass?, parameterName?, defaultValue?, nodeId?, posX?, posY?}]`，支持单次混合 `add_node` + `set_node` + `recompile`
  - `manage_material_wires`：顶层保留 `assetPath`，单项参数 → `wires:[{action, sourceNodeId?, sourceOutputName?, targetNodeId?, targetInputName?}]`

  所有批量模式统一返回 `{ assetPath/共享定位字段, totalCount, successCount, failCount?, results:[ {…每项字段, error?} ] }`，单项失败不中断其他项；对蓝图 / 结构体 / WBP / DataTable 等底层资产，批量结束后才统一标脏+重编译，大幅降低连续操作的引擎开销。结合第 1、2 批的 13 个高频工具，NexusLink 至此**26 个工具全部原生批量**，Schema 中彻底没有单数参数残留
- **BREAKING**: 批量参数统一升级 第 2 批 —— 再 8 个写入/调用类工具的单数参数被**移除**，统一改为 `updates[]` / `calls[]` / `operations[]` / 复数纯值数组形态（发版时对应 major 升级）：
  - `set_asset_property`：顶层保留 `assetPath`，`propertyPath`/`value`/`widgetName` → `updates:[{propertyPath, value, widgetName?}]`
  - `set_actor_property`：`actorName`/`propertyPath`/`value` → `updates:[{actorName, propertyPath, value}]`（允许单次跨多个 Actor 批改）
  - `set_runtime_widget_property`：`widgetName`/`propertyPath`/`value`/`ownerClass` → `updates:[{widgetName, propertyPath, value, ownerClass?}]`
  - `set_data_table_row`：顶层保留 `assetPath`，`rowName`/`fieldName`/`value` → `updates:[{rowName, fieldName, value}]`
  - `call_blueprint_function`：`actorName`/`functionName` → `calls:[{actorName, functionName}]`
  - `interact_widget`：`widgetName`/`action`/`value`/`ownerWidget` → `operations:[{widgetName, action, value?, ownerWidget?}]`
  - `manage_animation`：`actorName` → `actorNames: string[]`；`action`/`montagePath`/`playRate`/`startSection` 保留为共享字段（"对多个 Actor 做同一件事"场景）
  - `get_asset_property`：`assetPath` → `assetPaths: string[]`；与 `propertyPaths[]` 构成笛卡尔积（读端补齐多资产批量）

  所有批量模式仍统一返回 `{ totalCount, successCount, failCount?, results:[...] }`，每项回填定位字段 + 数据或 `error`，单项失败不中断其他项。结合第 1 批的 5 个只读/销毁工具，NexusLink 至此共有 13 个高频工具原生支持批量，彻底消除"N 次串行调用同一工具"反模式
- **BREAKING**: 批量参数统一升级 第 1 批 —— 5 个工具的单数参数被**移除**（此为破坏性变更，发版时对应 major 升级）：
  - `destroy_actor`：`actorName` → `actorNames: string[]`（必填）
  - `get_asset_refs`：`assetPath` → `assetPaths: string[]`（必填）
  - `get_data_table_row`：`rowName` → `rowNames: string[]`（必填）
  - `get_animation`：`actorName` → `actorNames: string[]`（必填）
  - `get_runtime_widget_property`：`widgetName` + `propertyPath` → `widgetNames: string[]` + `propertyPaths?: string[]`（笛卡尔积，`propertyPaths` 省略时每个 widget 返回 `children`）

  所有批量模式统一返回 `results[]` 数组，每项回填定位字段（`actorName`/`assetPath`/`rowName`/`widgetName`）+ 数据或 `error`，单项失败不中断其他项。AI 从此对这 5 个工具一次调用即可覆盖 N 个目标，彻底消除高频串行场景
- feat: `NexusUnreal规范.mdc` 新增「批量参数（单选 → 多选）硬性约束」章节 —— 明确何时必须提供批量参数、三种批量形态（复数纯值数组 / `updates[]` / `operations[]`）的使用场景、命名约定（单数 + `s`，禁止 `List`/`Array`/`Multi` 等别名）、返回体 `results[]` + 定位字段回填规则、以及破坏性变更的 major 升级流程。同步 `nexus-ai/rules/NexusMCP调用规范.mdc` §1 的"并行 > 串行，批量 > 逐条"速查表
- fix(script): 根 `build.bat` 与 `nexus-rider/build.bat` 移除末尾的 `pause` 指令 —— 脚本原本为资源管理器双击启动场景设计，但会在命令行 / CI / AI（含 Cursor 执行）调用时卡死在 `Press any key to continue`。现在跑完直接返回 exit code，需要保留窗口查看结果的用户从 `cmd` / `PowerShell` 里调用即可，双击场景窗口闪退可用右键"在新窗口中运行"替代

## [1.4.0] - 2026-04-20

- docs: `docs/tool-reference.md` 的 `manage_material` 章节新增「推荐模式：两步法」小节 —— 强调 Texture 参数节点（`TextureSampleParameter2D` 等）建议 `add_node` 只负责创建、`set_node` 单独设置 `parameterName` + `defaultValue`，失败隔离与 `appliedFields[]` 对账更清晰；同步 `nexus-ai/rules/NexusMCP调用规范.mdc` 添加材质编辑分组硬性约束
- perf: 补齐 `NexusPropertyUtils.cpp` 运行时错误消息英文化 —— `1.3.2` 的全量英文化只扫了 `Tools/` 目录，`Private/` 根目录下的 `NexusPropertyUtils`（承载 `get_asset_property` / `set_asset_property` 深度路径解析）14 处 `OutError` 中文字符串本轮一并改为英文（如"属性 '%s' 不支持深入访问" → "Property '%s' does not support nested access"），AI 读取路径错误时 token 消耗进一步下降；代码注释保留中文
- feat: `get_asset_property` / `set_asset_property` 新增容器下标穿透 —— `NexusPropertyUtils` 的路径解析器支持 `[i]`（数组数字下标）、`["key"]` / `['key']`（Map 字符串键，与 `KeyProp->ExportText` 结果等值比较）、`[elem]`（Set 元素匹配），并支持多级链式 `Matrix[0][1]` / `Users["alice"].Modules[2].Name`；元素若为 struct / object ref 可继续递归 `.` 钻取；`FArrayProperty::Inner` / `FMapProperty::KeyProp|ValueProp` / `FSetProperty::ElementProp` 的读写均被覆盖，贯通 Blueprint 变量、SCS 组件、Material 节点、DataAsset 等所有路径。字符串 Map Key 不能包含 `.`（路径分段分隔符）
- feat: `get_asset_property` / `set_asset_property` 新增 Material / MaterialFunction 表达式节点级穿透 —— `propertyPath` 首段匹配 `UMaterialExpression::GetName()`（即 `manage_material add_node` 返回的 `nodeId`，如 `MaterialExpressionTextureSampleParameter2D_0`）时，后续段对该表达式对象反射读写（如 `MaterialExpressionTextureSampleParameter2D_0.Texture` / `.ParameterName`），返回体携带 `nodeId` / `nodeClass`；`set_asset_property` 写入后自动 `PostEditChange`，Material 额外调用 `UMaterialEditingLibrary::RecompileMaterial` 触发重编译，打通"添加节点 → 反射读写节点字段 → 重编译"全链路
- fix: `manage_material` 的 `add_node` / `set_node` 现在支持 Texture 类节点（`TextureSample` / `TextureSampleParameter2D` 等 `UMaterialExpressionTextureBase` 子类）的贴图资产绑定 —— 通过 `defaultValue`（或 `value` 别名）传入贴图 `AssetPath`，内部 `LoadObject<UTexture>` 后写入 `Texture` 字段并自动调用 `AutoSetSampleType()` 推导 `SamplerType`；同时修复 `parameterName` 对 `UMaterialExpressionTextureSampleParameter` 不生效的问题（该类继承自 `TextureSample` 而非 `Parameter`，原单一 `Cast<UMaterialExpressionParameter>` 静默漏过）
- fix: `manage_material` 的 `set_node` 修复"静默成功"缺陷 —— 此前 `defaultValue` 传给 Texture 节点、或 `parameterName` 传给非参数类节点时，cast 失败即跳过但仍返回 `success:true`；现改为任何请求字段写入失败即返回 `success:false` + `fieldErrors[]` 详单，并要求至少提供一个字段才接受调用；`add_node` 同步补齐 `appliedFields[]` / `fieldErrors[]` 返回体，节点创建后字段写失败时 `success:false` 携带 `nodeId` 便于调用方 `remove_node` 回滚
- feat: 新增 AI 反馈采集闭环 —— 内置 `FNexusFeedbackCollector` 单例，dispatch 出口对错误/慢调用（>5s）/超大响应（>50KB）/30s 内重复模式（同 tool+argsDigest ≥3 次）四类信号自动埋点，落盘到 `<ProjectRoot>/.nexus-feedback/feedback.jsonl`，正常调用 0 写盘；脱敏：长字符串截断 200 字符、含 password/token/secret/apikey 关键字字段以 `<redacted>` 占位、嵌套 array/object 仅记结构摘要；超 5 MB 自动滚动归档；可通过 `Config/DefaultEngine.ini [NexusFeedback] bDisable=true` 关闭
- feat: 新增 MCP 工具 `submit_feedback` —— AI 显式上报反馈条目到本地 JSONL（`category`: tool_error/bad_error_msg/missing_tool/force_serial/schema_unclear/misc + `summary`/`relatedTool`/`suggestion`），由 `nexus-ai` 规范驱动 AI 在重试 ≥2 次失败、找不到合适工具、错误消息无法自修正、必须串行 ≥3 次拼结果、schema 字段含义靠猜等场景自动调用，零网络外发
- feat: 新增 MCP 工具 `export_feedback` —— 一键拼装 Markdown/JSON 反馈报告（分组：分类汇总 / Top 痛点工具 / AI 显式上报 / 自动埋点样本），可同时落盘到 `.nexus-feedback/report-<ts>.md` 供用户审阅后手动贴到项目 Issue，参数：`sinceHours`(默认 24, 0=全部) / `format`(markdown/json) / `writeToFile`(默认 true)
- feat: `get_asset` 新增 `section="defaults"` —— 返回 Blueprint CDO + NewVariables 当前默认值，含 C++ 父类继承的 `UPROPERTY(CPF_Edit)`（以 `inherited:true` 标记），配 `propertyPaths=[...]` 允许名单过滤首段名；`section="all"` 同步追加 `defaultsCount` 和 `sections` 覆盖清单字段，AI 据此判断是否仍需钻取子 section
- feat: `get_asset` 顶层支持 `assetPaths=[...]` 批量模式 —— 单次调用返回 `{results:[{assetPath, ...}]}`，单条失败不影响其他；报告场景下 3 个 BP 查询从 3 次合并为 1 次
- feat: BP graph overview（`section="graph"` 无 `graphName` / `section="all"`）增加 `enabledNodeCount` / `disabledNodeCount` —— AI 判"图表是否全禁用"不必再发一次完整节点查询
- fix: `get_asset_property` BP 分支新增 CDO 反射回退 —— 未命中 `NewVariables` 和 SCS 时自动反射 `GeneratedClass->GetDefaultObject()`，覆盖 C++ 父类暴露的 `UPROPERTY(EditDefaultsOnly)` 属性（如 `bUseBuffClass`），命中时返回体带 `inherited:true`
- feat: `/status` 端点返回体新增 `netRole` 字段 —— 根据当前 PIE/Game World 的 NetMode 推断 `DedicatedServer/ListenServer/Client/Standalone`，空闲编辑器为 `Editor`；供 VSCode / Rider 代理的 `list_unreal_instances` 直出，AI 可按角色选择目标实例
- perf: `initialize.instructions` 追加批量 + 多实例提示 —— 明确 `section=all` 覆盖范围禁止冗余子 section、`assetPaths[]` 多资产一次查、`arguments.targetPort` 多实例路由
- docs: 新增 MIT LICENSE 文件
- perf: 全量英文化 MCP 工具运行时错误与返回消息 —— `Tools/` 下 46 个工具源文件共 326 处 `R.Content` / `FString::Printf` 中的中文字符串改为等价英文表述（"未找到蓝图" → "Blueprint not found" 等），降低 AI 读取错误消息时的 token 消耗并提升自修正准确性；代码注释与 `UE_LOG` 保持中文不变

## [1.3.1] - 2026-04-17

- perf: 全量优化 MCP 工具 Schema token 消耗 —— 新增 `NexusMcpSchemaBuilder.h` (`NexusSchema` 命名空间) 统一构造 JSON Schema，强制使用 `enum`/`default`/`minimum`/`maximum` 原生字段替代 description 文本；`Def.Description` 统一收紧到 ≤ 80 字符、参数 `description` 收紧到 ≤ 50 字符；`tools/list` payload 预估 token 减少约 38%（~8500 → ~5310）
- perf: 瘦身 `initialize.instructions` —— 从 ~4700 字符 / ~1170 tokens 精简到 ~1200 字符 / ~260 tokens（-77%），冗长的工作流示例迁移到 `docs/tool-reference.md`
- feat: 新增 `nexus-unreal/Script/check_tool_tokens.py` —— 静态扫描所有工具 C++ 文件，输出 Top-N 超标排行与 token 预算审计；支持 `--json` / `--strict` 模式
- feat: `build_test.py` 构建完成后自动调用 token linter，将审计结果追加写入 `Saved/Logs/Build.Log`（仅告警，不阻断构建）
- chore: 打包产物重命名 `NexusLink-<ver>.zip` → `nexus-mcp-unreal-<ver>.zip`，与其他子模块命名对齐
- **breaking**: 返回字段重命名，统一语义（字段实际为 UserWidget **类名**非实例名）：`list_runtime_widgets` 的 `ownerWidget` → `widgetClass`；`get_slate_widget` 的 `umgWidget.ownerWidget` → `umgWidget.ownerWidgetClass`。外部调用方读该字段的代码需同步更新
- fix: `get_editor_info` 工具 Description 修正 — 原宣称返回 `projectDir` / `isPIERunning` 实际未输出，改为按实际字段 `engineVersion` / `projectName` / `platform` / `buildConfig`

## [1.3.0] - 2026-04-16

- refactor: `capture_viewport` 代码优化 — 提取 `CropToScreenRect` / `SaveAndBuildResult` 公共函数消除重复，PIE 弹出/恢复改用 RAII 守卫，`Execute` 拆分为 `HandleListTarget` / `HandleViewAngleCapture` / `CaptureByTarget` 子函数，删除无调用的 `CaptureViewportPixels`
- feat: 重构 `capture_viewport` 工具 — 改用 Win32 PrintWindow 截图方案，支持整个编辑器窗口截图及任意面板区域截图（viewport / content_browser / scene_outliner / details / output_log 等），新增 `target='list'` 列出可用面板
- feat: `capture_viewport` 新增 `actorName` 参数 — 截取视口并裁切到指定 Actor 的屏幕包围盒区域（支持编辑器和 PIE 两种模式，自动 8 角点世界→屏幕投影）
- feat: `capture_viewport` 新增 `widgetName` 参数 — 截取 PIE 视口并裁切到指定运行时 UMG Widget 的屏幕区域（可选 `ownerClass` 过滤）
- feat: `capture_viewport` 新增 `padding` 参数 — 控制 Actor 包围盒裁切的外扩比例（默认 10%）
- refactor: `capture_viewport` 改用 UE 原生跨平台方案 — 视口类 target 用 `FViewport::ReadPixels()`，窗口/面板类 target 用 `FWidgetRenderer`，移除全部 Win32 平台特定代码，Mac/Linux 可用
- feat: `capture_viewport` 自动清理旧截图，临时目录保留最近 20 张
- fix: `capture_viewport` FWidgetRenderer gamma 双重校正导致截图偏亮，修正为 `bUseGammaCorrection=true` + `bForceLinearGamma=true`
- feat: `capture_viewport` 新增 `windowIndex` 参数 — 支持截取任意顶层窗口（浮动面板、独立编辑器窗口等），`target='list'` 同时返回 windows 列表
- feat: `capture_viewport` 新增 `viewAngle` 参数 — 通过 `USceneCaptureComponent2D` 从指定角度拍摄 Actor（front/back/left/right/top/bottom），编辑器和 PIE 下均可用，不影响当前相机

## [1.2.0] - 2026-04-16

- feat: 新增 `create_behavior_tree` 工具 — 创建 BehaviorTree 资产，可选同时创建关联的 BlackboardData
- feat: 新增 `create_anim_blueprint` 工具 — 创建 AnimBlueprint 资产（关联指定 Skeleton，自动编译）
- feat: 新增 `create_anim_montage` 工具 — 创建 AnimMontage 资产（关联指定 Skeleton）
- feat: 新增 `exec_command` 工具 — 执行 UE 控制台命令并捕获输出（stat fps / show collision / slomo 等）
- feat: 新增 `get_asset_refs` 工具 — 查询资产依赖项或引用者（AssetRegistry API，支持递归/过滤/分页）
- feat: 新增 `get_animation` 只读工具 — 查询 Actor 动画运行时状态（section=state/slots/variables）
- feat: 新增 `manage_animation` 工具 — 控制 Actor 动画播放（action=play_montage/stop_montage）
- feat: 新增 `get_gameplay_tags` 工具 — 查询 Gameplay Tags 层级树/Actor Tags/资产 Tag 属性
- feat: 新增 `get_behavior_tree` 工具 — 检查行为树节点结构/Blackboard Keys/运行时 AI 执行状态
- feat: 新增 `capture_viewport` 工具 — 截取编辑器或 PIE 视口截图并返回文件路径
- feat: 兼容 UnLua 1.X 与 2.X — Build.cs 读取 `UnLua.uplugin` VersionName 自动注入 `UNLUA_VERSION_MAJOR` 宏，`get_lua`/`manage_lua` 在两个版本下均可工作
- feat: `get_asset` Blueprint 概览自动检测 UnLua 绑定，返回 `luaModule` 和 `luaFilePath`
- feat: `get_lua` 新增 `section=binding`（UnLua 绑定查询）和 `section=object`（运行时 Actor Lua 实例数据）
- feat: `get_lua` 新增 `section=value`/`env`/`memory`/`metatable`/`loaded`
- feat: `manage_lua` 新增 `action=dofile`/`set`/`gc`/`hotreload`
- feat: 工具默认全部启用（替代原只读模式），新工具不再需要用户手动开启
- feat: `tools/list` 响应新增 MCP 规范 `annotations` 字段，根据工具 Tags 自动映射 `readOnlyHint`
- refactor: 拆分 Lua 工具为 `get_lua`（只读）+ `manage_lua`（写入），替换原 `get_lua_stack` 和 `eval_lua`
- refactor: 提取 Lua 工具共享函数到 `NexusLuaUtils.h`
- fix: 跨版本编译兼容修复 — Build.cs 移除 `Regex` 依赖（UE 5.0）；`UMaterial` 属性输入适配 `GetEditorOnlyData()` (5.1+)；`FAssetData::AssetClass` → `AssetClassPath` (5.1+)
- fix: `exec_command` — `GEngine->Exec` 失败时增加 `PlayerController::ConsoleCommand` 回退
- fix: `control_pie` — `action=status` 始终输出 `isPIERunning` 字段
- fix: `manage_asset` — 批量删除增加二轮重试，避免因依赖顺序导致失败

## [1.1.1] - 2026-04-15

- fix: `create_data_asset` 抽象类错误提示改为列举可用的非抽象 DataAsset 子类，明确 UE4 限制
- fix: `manage_blueprint_graph add_node` 自动尝试 K2_ 前缀，解决 GetActorLocation 等 Actor 函数查找失败
- fix: `manage_blueprint_graph set_node` 结果中增加更新后的 posX/posY，便于调用方验证
- fix: `manage_blueprint_graph` / `manage_blueprint_wires` 入参缺失时提前返回明确错误，消除 LogJson Null 日志
- fix: `manage_material_wires disconnect` 新增对 targetNodeId="Material" 的支持（清除材质属性输入）
- fix: `manage_material_wires disconnect_all` 同时扫描并清除材质自身属性输入（BaseColor 等）
- fix: `interact_widget` CheckBox read/toggle/uncheck 始终返回 `isChecked` 字段，避免调用方无法区分未勾选与字段缺失
- fix: `create_material` 传入 `parentMaterial` 但未指定 `type` 时自动推断为 MaterialInstance

## [1.1.0] - 2026-04-15

- feat: 新增 `eval_lua` 工具 — 运行时求值 Lua 代码（表达式或多行语句），返回值 + 类型，table 递归展开
- feat: 新增 `get_lua_stack` 工具 — 获取当前 Lua 调用栈（section 分层查询 summary/locals/upvalues/all、sourceFilter 帧过滤、frameIndex/frameIndices 定点钻取）
- feat: `NexusLink.Build.cs` 自动检测 UnLua 插件，条件编译 `WITH_UNLUA`，未安装时工具仍注册但返回不可用提示
- feat: 工具定义新增 `bForceDisabled` 强制禁用机制 — 可选依赖缺失时工具默认关闭、设置面板 checkbox 置灰不可勾选
- feat: 新增 `diff_actors` 工具 — 对比两个运行时 Actor 的属性差异
- feat: 新增 `get_slate_widget` 工具
- feat: 新增材质工具集（4 新工具 + 1 扩展）
- feat: `get_actor_property` 新增 `actorNames[]` 多 Actor 批量查询、`propertyPaths` 数组批量查询、组件树概览（`section=components`）
- feat: `get_actor_property` 新增诊断预设（`diagnose=rotation_chain|defaults|world_transform|visibility|transform`）、`section="attach_hierarchy"` 返回完整 Attach 层级
- feat: 属性读取结构体自动返回 value（FVector/FRotator/FTransform 等无需拆分叶子路径）
- feat: `get_asset` 新增 MaterialFunction 专用 handler、Blueprint `section="all"` 含 graph 概览、Material `section="all"` 合并 overview + params
- feat: `get_asset_property` 新增 `propertyPaths` 数组批量查询
- feat: `get_output_log` 新增 `textFilters` 数组参数（OR 匹配）
- feat: `list_assets` 通用化 — `assetType` 支持传入任意 UClass 名称
- feat: 新增工具启用/禁用设置面板、工具定义新增 `Tags` 标签数组、插件首次启动默认使用只读模式
- fix: 消除 UE 5.3-5.7 材质输入 API 废弃警告（`GetInputs`/`GetInputsView` → `GetInput(i)`）
- fix: 新增工具自动继承只读模式默认值（`KnownToolNames` 替代单一 bool）
- fix: 修复 UE 5.3~5.7 跨版本编译兼容性（EMaterialDomain 枚举重命名、TArray::Pop 废弃签名等）
- fix: 修复多 UE 实例同时启动时端口冲突未自动顺延
- fix: 修复切换工具启用模式后 AI 客户端工具列表不刷新
- fix: 修复 Runtime Widget 工具无法发现运行时动态添加的子控件
- fix: 修正 WebSocket 端口文档和客户端 fallback 值
- refactor: 合并 11 个资产读写工具为 3 个通用工具（`get_asset` / `get_asset_property` / `set_asset_property`），工具总数 49 → 41
- refactor: HTTP 通道实现 per-session Dispatcher 会话隔离（`Mcp-Session-Id` header），多 AI 客户端并发直连互不干扰
- refactor: `NexusMcpTags` 常量从头文件 `static` 改为 `extern` + `.cpp` 定义
- refactor: `DomainToString` / `ParseDomain` 改用反射 + `static_cast`，消除跨版本枚举名依赖
- perf: `FNexusMcpToolRegistry` 注册时缓存 Definition，`tools/list` 不再每次创建临时工具实例
- docs: 新增 `NexusMCP调用规范.mdc` — AI 调用 UE MCP 工具的最佳实践

## [1.0.1] - 2026-04-14

- refactor: 合并 `list_data_assets` 到 `list_assets`，assetType 新增 DataTable / DataAsset；合并 `add_data_table_row` + `remove_data_table_row` 为 `manage_data_table_row`（工具总数 47 → 45）
- refactor: 统一 manage_* 工具的 C++ 类名和文件名与工具名一致
- refactor: 引入 `NexusVersionCompat.h` 统一版本兼容宏
- fix: 修复 UE 4.26 ~ 5.7 全版本编译兼容性
- fix: 修复 11 个 .cpp 缺少 `#include "NexusVersionCompat.h"` 导致 UE 5.4+ 编译失败
- fix: 修复 UE 5.2 编译失败——Build.cs 中对 UE 5.2 关闭 C4668/C4067

## [1.0.0] - 2026-04-13

- feat: 实现 MCP 服务器框架（JSON-RPC 2.0 + MCP Streamable HTTP + SSE），跨 UE4.26+ / UE5 兼容
- feat: WebSocket 服务器端点 + GET /status 无状态探测端点
- feat: 实例注册机制（临时目录 `{PID}.json`），客户端零扫描发现活跃实例
- feat: 端口冲突自动检测与切换，设置面板实时冲突提示
- feat: Level Editor 工具栏状态组件 + 视口覆盖层显示端口号
- feat: 47 个 MCP 工具，覆盖蓝图 / DataTable / DataAsset / Struct / Widget / Actor / PIE 全领域
- perf: JSON 响应精简——省略默认值字段、布尔仅非默认时写入，工具描述 token 压缩 60-70%
- perf: MCP initialize instructions 内置标准工作流与 UE 陷阱提示

