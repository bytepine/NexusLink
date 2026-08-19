# NexusLink — UE MCP 插件

**语言 / Language**: **简体中文** · [English](README.en.md)

基于 Unreal Engine 的 MCP 集成插件，将 UE 项目上下文通过 MCP 协议暴露给 AI 工具。

> 支持 UE 4.26 及以上所有版本（含 UE5）。主模块 `Type: UncookedOnly`，**MCP 跑在 Editor 二进制（含 `-server`/`-game`）**；cooked 包不包含本模块。

## 安装与启用

从 [NexusLink Releases](https://github.com/bytepine/NexusLink/releases) 下载 `nexus-mcp-unreal-<version>.zip`，或克隆本仓库到项目的 `Plugins/Developer/NexusLink`。

1. 将插件放入 `Plugins/Developer/NexusLink`，在 **Edit → Plugins → Developer → NexusLink** 中启用并重启编辑器
2. **Edit → Editor Preferences → Plugins → NexusLink** — 勾选 **启用 MCP 服务器**（**默认关闭**）。勾选后即时启动 HTTP（`POST /stream`）与 WebSocket，并注册实例供客户端发现；取消勾选立即停止，**无需再重启**
3. （可选）无 UI 的编辑器启动（如 `UEEditor-Cmd`）可加 **`-EnableNexusMcp`** 或控制台 **`NexusLink.EnableMcp 1|0`**（会话级，不写盘；与 Preferences 为 OR）

GAS / Niagara 等 Capability 按宿主项目插件探测，NexusLink **不**在 `.uplugin` 里强制依赖。

未启用时：标题栏不显示端口、客户端扫描不到实例、直连 `http://127.0.0.1:45000/stream` 无响应。完整步骤见 [docs/usage-guide.md](docs/usage-guide.md)。

## 接入 AI 客户端

NexusLink 提供 HTTP `:45000` + WebSocket `:55000`。日常推荐经客户端代理（固定端口、多实例切换）；也可直连 UE。四端端口与开关层数见 [usage-guide §1](docs/usage-guide.md)。

| 客户端 | 端点 | 说明 |
|--------|------|------|
| **[NexusDesktop](https://github.com/bytepine/NexusDesktop)** | `:6700` | 独立托盘程序；Windows `Setup.exe` / macOS `.dmg`（不要下载 `*-update.zip`） |
| **[NexusRider](https://github.com/bytepine/NexusRider)** | `:6800` | Rider Marketplace 搜索 **Nexus MCP** |
| **[NexusVSCode](https://github.com/bytepine/NexusVSCode)** | `:6900` | 扩展商店搜索 **Nexus MCP** |
| 直连 UE | `:45000` | 不用代理；须自行指定 UE 端口 |

## 示例工程

公开示例 [NexusUnreal](https://github.com/bytepine/NexusUnreal)（ThirdPerson 模板 + UnLua + MCP 回归测试）。插件以子模块挂载，**不随示例仓分发**；克隆须 `--recurse-submodules` 或单独安装本插件。

## 能力范围

默认 **SearchMode**：`tools/list` 仅 3 个元工具（`search_capabilities` / `call_capability` / `submit_feedback`），按需发现 Capability。覆盖编辑器、蓝图、动画、材质、音频、AI / EQS、GAS、控件、Niagara、PIE 运行时、UnLua 等。完整参数见 [docs/tool-reference.md](docs/tool-reference.md)；SearchMode vs MultiTool 见 [docs/architecture.md](docs/architecture.md#暴露模式toolslistmode)。

## 文档

| 文档 | 受众 |
|------|------|
| [docs/usage-guide.md](docs/usage-guide.md) | 安装、设置面板、四端接入 |
| [docs/architecture.md](docs/architecture.md) | 分层、Capability 系统、暴露模式 |
| [docs/proxy-session.md](docs/proxy-session.md) | 代理会话层契约（TTL / degraded / 写门控） |
| [docs/tool-reference.md](docs/tool-reference.md) | Capability 参数手册（`py scripts/build_tool_reference.py` 生成） |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 新增 Capability、测试、打包、发版 |
| [Resources/CapabilitySpec.md](Resources/CapabilitySpec.md) | Capability 元数据规范 |
| [Resources/AIRules.mdc](Resources/AIRules.mdc) | IDE Rule 模板（复制到游戏项目，见 [usage-guide §2.8](docs/usage-guide.md#28-挂载-airules)） |
| [CHANGELOG.md](CHANGELOG.md) | 版本记录 |

## License

[MIT](LICENSE) © byteyang
