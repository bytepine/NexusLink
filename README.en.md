# NexusLink — Unreal Engine MCP

[简体中文](README.md) · **English**

**Read, modify, and debug Unreal Engine projects with AI.** NexusLink is a free, open-source Unreal Engine MCP plugin that connects AI clients such as Cursor, Claude Code, and Windsurf through the Model Context Protocol (MCP).

[Quick start](#quick-start) · [GitHub downloads](https://github.com/bytepine/NexusLink/releases) · [Documentation](docs/README.md) · [Changelog](CHANGELOG.md)

Start with [NexusDesktop](https://github.com/bytepine/NexusDesktop), or use the **Nexus MCP** IDE integration from [VS Marketplace](https://marketplace.visualstudio.com/items?itemName=byteyang.nexus-mcp-vscode) / [Open VSX](https://open-vsx.org/extension/byteyang/nexus-mcp-vscode), or [NexusRider](https://github.com/bytepine/NexusRider). Choose one client; each requires NexusLink in UE.

## What you can do

- **Editor and assets**: inspect project context and work with Blueprints, materials, animation, audio, DataTables, widgets, and Niagara; [preview edits and verify writes](docs/asset-edit-preview.md).
- **Runtime debugging**: inspect logs, Actors, animation, AI / EQS, and GAS in PIE, standalone Game, and Dedicated Server; Game / PIE with a viewport also supports [UMG interaction](docs/runtime-widgets.md).
- **Diagnostics and comparison**: [GAS activation diagnostics](docs/gas-retest.md), [behavior-tree / Blackboard condition replay](docs/bt-retest.md), and [performance comparison from stopped Traces](docs/trace-compare.md).
- **Editor automation**: [Slate inspection and interaction, Automation tests, and nonblocking Live Coding](docs/editor-toolsets.md).
- **Project discovery and UE 5.8 domains**: [settings/plugins, skills, semantic search, PCG execution and sampling, Niagara diagnostics/fixes, UMG structure, and inline screenshots](docs/editor-toolsets.md).
- **Version-aware backends**: [official UE 5.8 Toolsets behind existing interfaces and older-engine compatibility](docs/toolset-backends.md).
- **Animation and native Toolsets**: [Sequencer / Control Rig, deep Niagara editing, Game Features, and custom Toolset discovery](docs/animation-niagara-gamefeatures-toolsets.md).
- **Parameters and project data**: [PCG parameter editing, detailed PhysicsAsset edits, GameplayTags configuration, and DataRegistry queries](docs/pcg-physics-tags-dataregistry.md).
- **MVVM data binding**: [ViewModel / widget / WBP property paths, binding directions, and unary conversions](docs/mvvm-bindings.md) (UE 5.5+).

Supports **UE 4.26+ (including UE5)**. MCP runs in the editor and Development / DebugGame Game / DS builds; **Shipping builds strip the MCP server at compile time**. Available capabilities depend on the engine version, host type, and enabled plugins.

## Quick start

The recommended relay is [NexusDesktop](https://github.com/bytepine/NexusDesktop): a standalone tray app with a stable endpoint and UE instance switching, without an IDE extension.

### 1. Install and enable the UE plugin

1. Download `nexus-mcp-unreal-<version>.zip` from [NexusLink Releases](https://github.com/bytepine/NexusLink/releases). Place its `NexusLink/` directory in your project's `Plugins/` folder and confirm the file is at `Plugins/NexusLink/NexusLink.uplugin`.
2. Enable **Edit → Plugins → Developer → NexusLink**, then restart the editor.
3. Open **Edit → Editor Preferences → Plugins → NexusLink**, check **Enable MCP Server** (off by default), and verify the runtime status and title-bar MCP / WS ports.

### 2. Connect your AI client

1. Download the latest stable [NexusDesktop release](https://github.com/bytepine/NexusDesktop/releases): use `*-setup.exe` on Windows or `.dmg` on macOS.
2. Start NexusDesktop, check **Enable relay server** in the tray, and verify that the status line shows the intended UE project.
3. Choose **MCP client configuration…**, select **Streamable HTTP** and your AI client, then paste the generated configuration into that client.

An [illustrated walkthrough](docs/usage-guide.md#3-nexusdesktop) is available in Chinese. `*-update.zip` is an update package, not an installer.

### 3. Verify the connection

Restart the MCP session and ask the AI:

> Use MCP to report the current UE editor information.

Check the returned project, engine version, and runtime state. Default **SearchMode** discovers and calls capabilities through three meta-tools: `SearchCapabilities`, `CallCapability`, and `SubmitFeedback`. NexusDesktop adds two instance tools.

If the connection fails, check the UE and relay switches first, then see [troubleshooting](docs/usage-guide.md#6-常见问题) (Chinese).

## Connection options

Choose one local proxy, or connect directly to UE. Avoid multiple proxies scanning and connecting to the same instance.

| Connection | Default MCP endpoint | Use case |
|------------|----------------------|----------|
| [NexusDesktop](https://github.com/bytepine/NexusDesktop) (recommended) | `http://127.0.0.1:6700/stream` | Standalone tray app; no IDE extension |
| [NexusRider](https://github.com/bytepine/NexusRider) | `http://127.0.0.1:6800/stream` | Rider integration; search **Nexus MCP** in Marketplace |
| [NexusVSCode](https://github.com/bytepine/NexusVSCode) | `http://127.0.0.1:6900/stream` | VSCode / Cursor / CodeBuddy / Windsurf extension |
| Direct UE | `http://127.0.0.1:45000/stream` | Configure a Bearer token directly; no proxy cache, write gate, or instance switching |

UE defaults to HTTP `:45000` and WebSocket `:55000`; it advances to available ports on conflict. Use the ports shown in the UI. See the [usage guide](docs/usage-guide.md#1-选端与开关) for authentication, switches, and remote connections (Chinese).

## Runtime and extensions

- **Standalone Game / DS**: append `-EnableNexusMcp` to Development / DebugGame launch arguments; these processes do not inherit editor Preferences. Game / PIE can use `NexusLink.Mcp panel`; DS has no viewport, so use `NexusLink.Mcp status`. See [runtime debugging](docs/usage-guide.md#241-独立-game--ds-mcp-debug-清单) (Chinese).
- **UnLua extension**: install the separately released [NexusLinkExt](https://github.com/bytepine/NexusLinkExt) for runtime Lua debugging and Blueprint binding, then discover its capabilities with `SearchCapabilities` and `lua`. See [installation](docs/usage-guide.md#可选安装-nexuslinkextunlua) (Chinese).
- **Example project**: [NexusUnreal](https://github.com/bytepine/NexusUnreal) includes a ThirdPerson template and MCP regression tests. Plugins are git submodules; clone with `--recurse-submodules` or install the plugin separately.

## Security and write boundaries

- **Authentication is on by default**. Treat tokens as credentials: never commit them, publish them in documentation, or expose them in screenshots.
- **Dangerous capabilities are disabled by default**. `ExecCommand`, `ExecPython`, and editor UI input, test control, and compilation require explicit authorization. See [access modes and script risks](docs/usage-guide.md#210-危险-capability-与脚本执行) (Chinese). Prefer read-only `GetPythonApi` for API inspection.
- **Writes change real assets or runtime state**. Undo and batch rollback do not cover every side effect. Review version-control changes before committing; see [write and rollback boundaries](docs/usage-guide.md#修改了属性但-ue-中没有生效--磁盘未变化) (Chinese).

When upgrading from 1.x, upgrade the plugin and clients to 2.x together. See the [migration guide](docs/usage-guide.md#0-从-1x-升级到-20) for authentication and MCP text changes (Chinese).

## Documentation and development

- [Documentation index](docs/README.md): find setup, workflows, diagnostics, and development guides. Most guides are in Chinese; bilingual pages and English references are marked.
- [Capability reference](docs/tool-reference.md) / [简体中文](docs/tool-reference.zh.md): tools, parameters, and response conventions.
- [Contributing](CONTRIBUTING.md): adding capabilities, testing, packaging, and releases. Design details: [architecture](docs/architecture.md) and [Capability specification](Resources/CapabilitySpec.md) (Chinese).

## License

[MIT](LICENSE) © byteyang
