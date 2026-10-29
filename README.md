# NexusLink — Unreal Engine MCP

**简体中文** · [English](README.en.md)

**让 AI 读取、修改和调试 Unreal Engine 项目。** NexusLink 是免费开源的 Unreal Engine MCP 插件，通过 Model Context Protocol（MCP）连接 Cursor、Claude Code、Windsurf 等 AI 客户端。

[快速开始](#快速开始) · [GitHub 下载](https://github.com/bytepine/NexusLink/releases) · [文档导航](docs/README.md) · [更新记录](CHANGELOG.md)

首次使用推荐 [NexusDesktop](https://github.com/bytepine/NexusDesktop)；已有 IDE 工作流可选 **Nexus MCP** 扩展：[VS Marketplace](https://marketplace.visualstudio.com/items?itemName=byteyang.nexus-mcp-vscode) / [Open VSX](https://open-vsx.org/extension/byteyang/nexus-mcp-vscode)，或 [NexusRider](https://github.com/bytepine/NexusRider)。这些客户端任选其一，UE 侧都需安装 NexusLink。

## 能做什么

- **编辑器与资产**：查询项目上下文，读写蓝图、材质、动画、音频、DataTable、Widget 和 Niagara 等资产；支持[修改前预览与写后校验](docs/asset-edit-preview.md)。
- **运行时调试**：在 PIE、独立 Game 和 Dedicated Server 中检查日志、Actor、动画、AI / EQS 和 GAS；有视口的 Game / PIE 还支持 [UMG 控件交互](docs/runtime-widgets.md)。
- **复测与性能分析**：[GAS 技能激活诊断](docs/gas-retest.md)、[行为树 / Blackboard 条件复测](docs/bt-retest.md)、[已停录 Trace 的同条件性能对比](docs/trace-compare.md)。
- **编辑器自动化**：[Slate 界面查询与交互、Automation 测试、非阻塞 Live Coding](docs/editor-toolsets.md)。
- **项目发现与 5.8 领域诊断**：[设置/插件、技能、语义搜索、PCG 执行采样、Niagara 诊断/修复、UMG 结构与标准截图图片](docs/editor-toolsets.md)。
- **参数与项目数据编辑**：[PCG 参数闭环、PhysicsAsset 精细编辑、GameplayTags 配置与 DataRegistry 查询](docs/pcg-physics-tags-dataregistry.md)。
- **MVVM 数据绑定**：[ViewModel / 控件 / WBP 属性路径、绑定方向和单输入转换函数](docs/mvvm-bindings.md)（UE 5.5+）。

支持 **UE 4.26+（含 UE5）**。MCP 可运行于编辑器及 Development / DebugGame 的 Game / DS；**Shipping 在编译期剔除 MCP 服务器**。具体能力随引擎版本、宿主类型和已启用插件裁剪。

## 快速开始

推荐通过独立托盘应用 [NexusDesktop](https://github.com/bytepine/NexusDesktop) 接入：无需 IDE 扩展，端点固定，支持多 UE 实例切换。

### 1. 安装并启用 UE 插件

1. 从 [NexusLink Releases](https://github.com/bytepine/NexusLink/releases) 下载 `nexus-mcp-unreal-<version>.zip`，将其中的 `NexusLink/` 目录放入项目 `Plugins/`，确认文件位于 `Plugins/NexusLink/NexusLink.uplugin`。
2. 在 **Edit → Plugins → Developer → NexusLink** 中启用插件，重启编辑器。
3. 在 **Edit → Editor Preferences → Plugins → NexusLink** 中勾选 **启用 MCP 服务器**（默认关闭），确认运行状态与标题栏的 MCP / WS 端口。

### 2. 连接 AI 客户端

1. 从 [NexusDesktop Releases](https://github.com/bytepine/NexusDesktop/releases) 下载最新正式版：Windows 选 `*-setup.exe`，macOS 选 `.dmg`。
2. 启动 NexusDesktop，在托盘勾选 **启用中转服务器**，确认状态行显示目标 UE 项目。
3. 选择 **MCP 客户端配置…**，选 **Streamable HTTP** 和所用 AI 客户端，将生成的配置粘贴到客户端。

首次接入可跟随[图文步骤](docs/usage-guide.md#3-nexusdesktop)。`*-update.zip` 用于更新，不是安装包。

### 3. 验证连接

重启 MCP 会话，向 AI 输入：

> 使用 MCP 告诉我当前 UE 编辑器信息。

核对返回的工程、引擎版本和运行状态。默认 **SearchMode** 通过 `SearchCapabilities`、`CallCapability`、`SubmitFeedback` 三个元工具按需发现和调用能力；NexusDesktop 另外提供两个实例工具。

连接失败时，先检查 UE 和中转两端的开关，再看[常见问题](docs/usage-guide.md#6-常见问题)。

## 连接方式

任选一个本机代理，或直连 UE；避免多个代理重复扫描并连接同一实例。

| 接入方式 | 默认 MCP 端点 | 适用场景 |
|----------|---------------|----------|
| [NexusDesktop](https://github.com/bytepine/NexusDesktop)（推荐） | `http://127.0.0.1:6700/stream` | 独立托盘，无需 IDE 扩展 |
| [NexusRider](https://github.com/bytepine/NexusRider) | `http://127.0.0.1:6800/stream` | Rider 内使用，Marketplace 搜索 **Nexus MCP** |
| [NexusVSCode](https://github.com/bytepine/NexusVSCode) | `http://127.0.0.1:6900/stream` | VSCode / Cursor / CodeBuddy / Windsurf 扩展 |
| 直连 UE | `http://127.0.0.1:45000/stream` | 直接配置 Bearer Token；无代理缓存、写入前确认与实例切换 |

UE 默认监听 HTTP `:45000` 和 WebSocket `:55000`；冲突时自动切换端口，以界面显示为准。身份验证、各端开关及跨机接入见[使用指南](docs/usage-guide.md#1-选端与开关)。

## 运行时与扩展

- **独立 Game / DS**：Development / DebugGame 启动参数追加 `-EnableNexusMcp`，不继承编辑器 Preferences 开关。Game / PIE 可用 `NexusLink.Mcp panel` 查看状态；DS 无视口，使用 `NexusLink.Mcp status`。详见[运行时调试](docs/usage-guide.md#241-独立-game--ds-mcp-debug-清单)。
- **UnLua 扩展**：安装独立发布的 [NexusLinkExt](https://github.com/bytepine/NexusLinkExt)，获得运行时 Lua 调试和蓝图绑定，再用 `SearchCapabilities` 搜索 `lua`。详见[安装步骤](docs/usage-guide.md#可选安装-nexuslinkextunlua)。
- **示例工程**：[NexusUnreal](https://github.com/bytepine/NexusUnreal) 提供 ThirdPerson 模板与 MCP 回归测试。插件以子模块挂载，克隆时使用 `--recurse-submodules`，或另行安装插件。

## 安全与写入边界

- **身份验证默认开启**。Token 是访问凭证，不要提交到仓库、写入公开文档或暴露在截图中。
- **危险能力默认禁用**。`ExecCommand`、`ExecPython` 及编辑器 UI 输入、测试控制、编译等能力须显式授权；访问模式和脚本风险见[危险 Capability](docs/usage-guide.md#210-危险-capability-与脚本执行)。只查询 Python API 时优先用只读 `GetPythonApi`。
- **写操作会修改真实资产或运行状态**。Undo 和批次回滚不覆盖所有副作用；提交前检查版本管理差异，具体边界见[写入与回滚说明](docs/usage-guide.md#修改了属性但-ue-中没有生效--磁盘未变化)。

从 1.x 升级时，插件与客户端须一起升至 2.x；身份验证和 MCP 文案变化见[升级指南](docs/usage-guide.md#0-从-1x-升级到-20)。

## 文档与开发

- [文档导航](docs/README.md)：按安装接入、日常操作、诊断复测和插件开发查找指南。
- [Capability 参数手册](docs/tool-reference.zh.md) / [English](docs/tool-reference.md)：工具、参数与返回约定。
- [贡献指南](CONTRIBUTING.md)：新增 Capability、测试、打包与发版；设计背景见[架构](docs/architecture.md)和[Capability 规范](Resources/CapabilitySpec.md)。

## License

[MIT](LICENSE) © byteyang
