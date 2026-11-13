# NexusLink — Unreal Engine MCP

[简体中文](README.md) · **English**

**Read, modify, and debug Unreal Engine projects with AI.** NexusLink is a free, open-source Unreal Engine MCP plugin that connects AI clients such as Cursor, Claude Code, and Windsurf through the Model Context Protocol (MCP).

[Quick start](#quick-start) · [GitHub downloads](https://github.com/bytepine/NexusLink/releases) · [Documentation](docs/README.en.md) · [Changelog](CHANGELOG.en.md)

Start with [NexusDesktop](https://github.com/bytepine/NexusDesktop), or use the **Nexus MCP** IDE integration from [VS Marketplace](https://marketplace.visualstudio.com/items?itemName=byteyang.nexus-mcp-vscode) / [Open VSX](https://open-vsx.org/extension/byteyang/nexus-mcp-vscode), or [NexusRider](https://github.com/bytepine/NexusRider). Choose one client; each requires NexusLink in UE.

## What you can do

- **Asset editing**: read and modify Blueprints, materials, animation, audio, UMG, Niagara, and more, with [edit previews, batch changes, and write verification](docs/asset-edit-preview.en.md).
- **Scenes and projects**: manage levels, Actors, [navigation and collision](docs/editor-compatibility.en.md#导航与碰撞诊断); edit project settings and [PCG, PhysicsAsset, GameplayTags, and other data](docs/pcg-physics-tags-dataregistry.en.md).
- **Runtime debugging**: inspect logs, Actors, animation, AI, and GAS in PIE, Game, and DS, with [input injection and UI diagnostics](docs/runtime-diagnostics.en.md) and [UMG interaction](docs/runtime-widgets.en.md).
- **Diagnostics and performance**: [GAS activation diagnostics](docs/gas-retest.en.md), [behavior-tree condition replay](docs/bt-retest.en.md), and [Trace performance comparison](docs/trace-compare.en.md).
- **Editor automation**: [graph discovery, node selection and navigation, view controls, Slate UI fallback, Automation tests, and Live Coding](docs/editor-toolsets.en.md). Prefer dedicated domain and graph MCP tools.

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

An [illustrated walkthrough](docs/usage-guide.en.md#3-nexusdesktop) is available. `*-update.zip` is an update package, not an installer.

### 3. Verify the connection

Restart the MCP session and ask the AI:

> Use MCP to report the current UE editor information.

Check the returned project, engine version, and runtime state. Default **SearchMode** discovers and calls capabilities through three meta-tools: `SearchCapabilities`, `CallCapability`, and `SubmitFeedback`. NexusDesktop adds two instance tools.

A `GetAsset` query points to a capability for the asset type, such as `GetAssetBlueprint`; use `SearchAsset` to find asset paths.

If the connection fails, check the UE and relay switches first, then see [troubleshooting](docs/usage-guide.en.md#6-faq).

## Connection options

Choose one local proxy, or connect directly to UE. Avoid multiple proxies scanning and connecting to the same instance.

| Connection | Default MCP endpoint | Use case |
|------------|----------------------|----------|
| [NexusDesktop](https://github.com/bytepine/NexusDesktop) (recommended) | `http://127.0.0.1:6700/stream` | Standalone tray app; no IDE extension |
| [NexusRider](https://github.com/bytepine/NexusRider) | `http://127.0.0.1:6800/stream` | Rider integration; search **Nexus MCP** in Marketplace |
| [NexusVSCode](https://github.com/bytepine/NexusVSCode) | `http://127.0.0.1:6900/stream` | VSCode / Cursor / CodeBuddy / Windsurf extension |
| Direct UE | `http://127.0.0.1:45000/stream` | Configure a Bearer token directly; no proxy cache, write gate, or instance switching |

UE defaults to HTTP `:45000` and WebSocket `:55000`; it advances to available ports on conflict. Use the ports shown in the UI. See the [usage guide](docs/usage-guide.en.md#1-clients-and-switches) for authentication, switches, and remote connections.

## Runtime and extensions

- **MCP status query**: C++ and Blueprints can query the actual server state with `UNexusLinkSettings::IsMcpEnabled()`. It returns `false` when the server has not started, startup failed, or the build is Shipping. See [runtime status](docs/usage-guide.en.md#23-verify-runtime-state).
- **Standalone Game / DS**: append `-EnableNexusMcp` to Development / DebugGame launch arguments; these processes do not inherit editor Preferences. Game / PIE can use `NexusLink.Mcp panel`; DS has no viewport, so use `NexusLink.Mcp status`. See [runtime debugging](docs/usage-guide.en.md#241-standalone-game--ds-debugging-checklist).
- **UnLua extension**: install the separately released [NexusLinkExt](https://github.com/bytepine/NexusLinkExt) for runtime Lua debugging and Blueprint binding, then discover its capabilities with `SearchCapabilities` and `lua`. See [installation](docs/usage-guide.en.md#optional-nexuslinkext-unlua).
- **Example project**: [NexusUnreal](https://github.com/bytepine/NexusUnreal) includes a ThirdPerson template and MCP regression tests. Plugins are git submodules; clone with `--recurse-submodules` or install the plugin separately.

## Security and write boundaries

- **Authentication is on by default**. Treat tokens as credentials: never commit them, publish them in documentation, or expose them in screenshots.
- **Only arbitrary code-execution entry points are dangerous capabilities, and they are disabled by default**. That set is `ExecCommand`, `ExecPython`, and NexusLinkExt Lua execution. See [access modes and script risks](docs/usage-guide.en.md#210-dangerous-capabilities-and-scripts). Prefer read-only `GetPythonApi` for API inspection.
- **Writes change real assets or runtime state**. Undo and batch rollback do not cover every side effect. Review version-control changes before committing; see [write and rollback boundaries](docs/usage-guide.en.md#property-changes-are-not-visible-in-ue-or-on-disk).

When upgrading from 1.x, upgrade the plugin and clients to 2.x together. See the [migration guide](docs/usage-guide.en.md#0-upgrade-from-1x-to-20) for authentication and MCP text changes.

## Documentation and development

- [Documentation index](docs/README.en.md): find setup, workflows, diagnostics, and development guides. Each guide has separate Chinese and English pages linked at the top.
- [Capability reference](docs/tool-reference.md) / [简体中文](docs/tool-reference.zh.md): tools, parameters, and response conventions.
- [Contributing](CONTRIBUTING.en.md): adding capabilities, testing, packaging, and releases. Design details: [architecture](docs/architecture.en.md) and [Capability specification](Resources/CapabilitySpec.en.md).

## License

[MIT](LICENSE) © byteyang
