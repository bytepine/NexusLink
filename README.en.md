**Language / Language**: [简体中文](README.md) · **English**

# NexusLink — UE MCP Plugin

An MCP integration plugin for Unreal Engine that exposes UE project context to AI tools via the MCP protocol.

> Supports UE 4.26 and all later versions (including UE5). Module `Type: Runtime`; **MCP only runs in Editor / PIE**.

## Installation & Enablement

Download `nexus-mcp-unreal-<version>.zip` from [NexusLink Releases](https://github.com/bytepine/NexusLink/releases), or clone this repository into your project's `Plugins/Developer/NexusLink`.

1. Place the plugin in `Plugins/Developer/NexusLink`, enable it under **Edit → Plugins → Developer → NexusLink**, and restart the editor
2. **Edit → Editor Preferences → Plugins → NexusLink** — check **Enable MCP Server** (**off by default**). Once checked, HTTP (`POST /stream`) and WebSocket start immediately and the instance is registered for client discovery; unchecking stops them immediately — **no second restart**
3. (Optional) For headless **editor** launches (e.g. `UEEditor-Cmd`), pass **`-EnableNexusMcp`** or console **`NexusLink.EnableMcp 1|0`** (session-only, does not write settings; OR with Preferences)

GAS / Niagara Capabilities are detected from the host project; NexusLink does **not** force those plugins via `.uplugin`.

When disabled: the title bar shows no port, clients cannot discover the instance, and a direct connection to `http://127.0.0.1:45000/stream` gets no response. Full steps: [docs/usage-guide.md](docs/usage-guide.md).

## Connect an AI client

NexusLink serves HTTP `:45000` + WebSocket `:55000`. Daily use: a client proxy (fixed port, multi-instance switching). You can also connect to UE directly. Ports and switch layers: [usage-guide §1](docs/usage-guide.md).

| Client | Endpoint | Notes |
|--------|----------|-------|
| **[NexusDesktop](https://github.com/bytepine/NexusDesktop)** | `:6700` | Standalone tray app; Windows `Setup.exe` / macOS `.dmg` (do not download `*-update.zip`) |
| **[NexusRider](https://github.com/bytepine/NexusRider)** | `:6800` | Rider Marketplace: **Nexus MCP** |
| **[NexusVSCode](https://github.com/bytepine/NexusVSCode)** | `:6900` | Extension marketplace: **Nexus MCP** |
| Direct UE | `:45000` | No proxy; you must specify the UE port |

## Example project

Public sample [NexusUnreal](https://github.com/bytepine/NexusUnreal) (ThirdPerson template + UnLua + MCP regression tests). The plugin is a git submodule and **is not bundled** with the sample; clone with `--recurse-submodules` or install this plugin separately.

## Coverage

Default **SearchMode**: `tools/list` exposes 3 meta-tools (`search_capabilities` / `call_capability` / `submit_feedback`); Capabilities are discovered on demand. Coverage includes editor, Blueprint, animation, material, audio, AI / EQS, GAS, UMG, Niagara, PIE runtime, UnLua, and more. Full parameters: [docs/tool-reference.md](docs/tool-reference.md). SearchMode vs MultiTool: [docs/architecture.md](docs/architecture.md#暴露模式toolslistmode).

## Documentation

| Doc | Audience |
|-----|----------|
| [docs/usage-guide.md](docs/usage-guide.md) | Install, settings panel, four clients |
| [docs/architecture.md](docs/architecture.md) | Layering, Capability system, exposure modes |
| [docs/tool-reference.md](docs/tool-reference.md) | Capability parameter reference (`py scripts/build_tool_reference.py`) |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Add Capabilities, tests, packaging, release |
| [Resources/CapabilitySpec.md](Resources/CapabilitySpec.md) | Capability metadata spec |
| [Resources/AIRules.mdc](Resources/AIRules.mdc) | IDE Rule template (copy into the game project; [usage-guide §2.8](docs/usage-guide.md#28-挂载-airules)) |
| [CHANGELOG.md](CHANGELOG.md) | Version history |

## License

[MIT](LICENSE) © byteyang
