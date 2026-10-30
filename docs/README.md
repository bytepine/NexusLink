# NexusLink 文档 / Documentation

[项目首页](../README.md) · [English README](../README.en.md)

首次使用从[快速开始](../README.md#快速开始)进入；已连接 MCP 后，按下面的任务选择指南。工具名称、可用性和参数以当前连接返回的清单为准。

Most guides are in Chinese. English readers can start with the [English quick start](../README.en.md#quick-start) and [English tool reference](tool-reference.md); GAS, asset-preview, and runtime-widget guides include English explanations.

## 安装与接入 / Setup

- [使用指南](usage-guide.md)：插件安装、客户端选择、开关和连接验证。
- [身份验证](usage-guide.md#11-身份验证)与[跨机接入](usage-guide.md#12-跨机显式-ip不扫网段)：Token、监听地址与远端配置。
- [独立 Game / Dedicated Server](usage-guide.md#241-独立-game--ds-mcp-debug-清单)：启动参数、宿主差异与运行时调试。
- [NexusLinkExt / UnLua 安装](usage-guide.md#可选安装-nexuslinkextunlua)：可选第三方插件能力。
- [1.x → 2.x 升级](usage-guide.md#0-从-1x-升级到-20)与[常见问题](usage-guide.md#6-常见问题)。

## 日常操作 / Workflows

- [资产修改预览](asset-edit-preview.md)：预览蓝图默认值、DataTable 字段和 Widget 属性，检查前值并校验写入。
- [运行时 UMG 交互](runtime-widgets.md)：按实例路径定位控件，处理重名、重建和可见性限制。
- [编辑器工具](editor-toolsets.md)：Slate 进阶交互、UMG 结构、标准截图图片、Automation / Live Coding、PCG / Niagara 诊断、语义搜索、技能及设置/插件发现。
- [MVVM 数据绑定](mvvm-bindings.md)：上下文与属性路径、绑定方向、单输入转换函数，以及编译后读回。
- [危险 Capability 与脚本执行](usage-guide.md#210-危险-capability-与脚本执行)：访问模式、确认流程、Python 前提和写入风险。
- [AIRules 模板接入](usage-guide.md#29-挂载-airules)：将规则装入游戏项目并维护项目定制项。

## 诊断与复测 / Diagnostics

- [GAS 激活诊断与复测](gas-retest.md)：显式端点和 Actor 映射、激活事件、HTML / JSON / JUnit 报告、离线基线对比。
- [行为树 / Blackboard 复测](bt-retest.md)：绑定目标、改变条件、等待状态、恢复原值并保存快照证据。
- [Trace 性能对比](trace-compare.md)：导出已停录 `.utrace` 的同条件快照，离线比较帧与 CPU 统计。

## 参数与插件开发 / Reference and development

- [Capability 参数手册（中文）](tool-reference.zh.md) / [English reference](tool-reference.md)：元工具、Capability 参数和响应约定；由脚本生成。
- [贡献指南](../CONTRIBUTING.md)：新增能力、测试、打包与发版；[E2E 测试指南](testing.md)说明宿主工程的回归测试组织。
- [架构设计](architecture.md)：模块职责、注册机制、SearchMode / MultiTool 和传输模型。
- [代理会话约定](proxy-session.md)：实例作用域、缓存、断线快照、写入前确认与大包保存。
- [Capability 规范](../Resources/CapabilitySpec.md)：模块与基类选择、元数据和接口契约。
- [版本兼容宏参考](version-compat-reference.md)：跨 UE 版本的语义宏与使用规则。
- [更新记录](../CHANGELOG.md)：版本历史与升级变化。

- [版本后端复用](toolset-backends.md)：既有查询和编辑接口复用 5.8 官方 Toolset，保留旧版契约。
- [UMG、材质、项目配置与 Sequencer 扩展](editor-remaining-toolsets.md)：后续领域、原生路由和旧版支持边界。
- [编辑器兼容扩展](editor-compatibility.md)：参数与 UMG、网格生成、场景整理、蓝图图编辑及各版本支持范围。
- [动画与原生 Toolset 扩展](animation-niagara-gamefeatures-toolsets.md)：Sequencer / Control Rig、Niagara 深层写入、Game Features 与自定义 Toolset 发现。
- [参数与项目数据](pcg-physics-tags-dataregistry.md)：PCG 调参、物理资产、Tag 配置与 DataRegistry。
