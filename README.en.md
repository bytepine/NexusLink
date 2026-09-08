**Language / Language**: [简体中文](README.md) · **English**

# NexusLink — UE MCP Plugin

An MCP integration plugin for Unreal Engine that exposes UE project context to AI tools via the MCP protocol.

> Supports UE 4.26 and all later versions (including UE5). Module `Type: UncookedOnly`; **MCP runs in the Editor binary (including `-server`/`-game`)** and is not compiled into cooked Game/Server packages.

> **Upgrading from 1.x**: 2.0 enables auth by default and switches all MCP/AI-facing text to English. Upgrade the plugin and clients to 2.x together — see [usage-guide §0](docs/usage-guide.md#0-从-1x-升级到-20) (Chinese).

## Installation & Enablement

Download `nexus-mcp-unreal-<version>.zip` from [NexusLink Releases](https://github.com/bytepine/NexusLink/releases), or clone this repository into your project's `Plugins/Developer/NexusLink`.

1. Place the plugin in `Plugins/Developer/NexusLink`, enable it under **Edit → Plugins → Developer → NexusLink**, and restart the editor
2. **Edit → Editor Preferences → Plugins → NexusLink** — check **Enable MCP Server** (**off by default**). HTTP (`POST /stream`) and WebSocket start immediately. **MCP Auth** is on by default. Tokens, extra tokens, and on/off combinations: [usage-guide §1.1](docs/usage-guide.md#11-鉴权). Binds loopback by default; for remote relays also check **Allow LAN bind** and use **Copy remote connection** to pick a NIC IP.
3. (Optional) For headless **editor** launches (e.g. `UEEditor-Cmd`), pass **`-EnableNexusMcp`** or console **`NexusLink.EnableMcp 1|0`** (session-only, does not write settings; OR with Preferences). Tests may pass **`-NexusEnableDangerousCaps`** to enable `exec_command` / `eval_runtime_lua` / `dofile_runtime_lua` / `exec_python`

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

Default **SearchMode**: `tools/list` exposes 3 meta-tools (`search_capabilities` / `call_capability` / `submit_feedback`); Capabilities are discovered on demand. Coverage includes editor, Blueprint, animation, material, audio, AI / EQS, GAS, UMG, Niagara, PIE runtime, UnLua, and more. Full parameters: [docs/tool-reference.md](docs/tool-reference.md) ([简体中文](docs/tool-reference.zh.md)). SearchMode vs MultiTool: [docs/architecture.md](docs/architecture.md#暴露模式toolslistmode).

## Dangerous Capabilities (disabled by default)

Four "script escape hatch" capabilities. They are written into the disabled list by name on install/upgrade, so they are **not callable out of the box**. Enable them individually under **Editor Preferences → Plugins → NexusLink**, or launch with `-NexusEnableDangerousCaps` (session-only, never written to settings). Ones you enabled by hand are not overwritten by later upgrades.

| Capability | What it does | Extra requirement |
|---|---|---|
| `exec_command` | Run a UE console command and capture its output | — |
| `exec_python` | Run Python in the editor (`exec` / `file` / `eval`); returns stdout and traceback | Python Editor Script Plugin enabled in the host project |
| `eval_runtime_lua` | Evaluate a Lua snippet in PIE/Game; returns stacked values | UnLua + PIE |
| `dofile_runtime_lua` | Load and run a `.lua` file from `Content/Script/` | UnLua + PIE |

> Read-only probing goes through **`get_python_api`**. It needs the same Python plugin but only runs `inspect`, embedding whitelist-validated arguments into a fixed script — it never accepts user code, so it is **enabled by default**. Use it to check whether an `unreal.*` API exists on this engine version instead of turning on `exec_python`.

### Why they are off by default

- **Equivalent to arbitrary in-process code execution**: Python / Lua can `import os`, touch any file, and spawn subprocesses. Once enabled, auth is the only remaining boundary and per-capability enable/disable stops meaning anything.
- **They bypass the write-path safety net**: every other Capability wraps its writes in `FNexusEditorTransaction` (undoable; a failed `calls[]` batch rolls back as a whole) and is subject to the proxy write gate and memory ledger. Edits made inside a script are outside that wrapper, so a bad AI edit cannot be rolled back.
- **They crash the editor easily**: model-written scripts routinely hit the wrong thread, stale objects, or GC — and the crash is hard to attribute afterwards.

### Trade-off versus the default-enabled Capabilities

| Aspect | Declarative Capability (default on) | Script escape hatch (default off) |
|---|---|---|
| Coverage | Only the domains already implemented | Whatever the engine exposes — full long tail |
| Arguments | JSON Schema validated; bad args fail fast with `arg_invalid` | Free-form text; errors surface as a runtime traceback |
| Response | Structured + `*_defaults` compaction, predictable token cost | Unstructured stdout, easy to blow up the response body |
| Undo | Uniform transaction wrapper | None |
| Control flow | `calls[]` batches only — no branching or loops | Arbitrary loops and branches in a single round trip |
| Version drift | `NX_*` semantic macros absorb 4.26–5.8 differences at compile time | Left to the script; UE4/UE5 API differences break easily |

### When to actually use them

Try `search_capabilities` for a dedicated cap first. Only three cases justify the escape hatch: the capability genuinely does **not** exist yet (send a `submit_feedback` while you are there), you need a batch with branching or loops, or you are debugging a capability itself. Turn it back off afterwards.

## Documentation

| Doc | Audience |
|-----|----------|
| [docs/usage-guide.md](docs/usage-guide.md) | Install, switch layers, **auth**, four clients |
| [docs/architecture.md](docs/architecture.md) | Layering, Capability system, exposure modes |
| [docs/proxy-session.md](docs/proxy-session.md) | Proxy session contract (TTL / degraded / write gate) |
| [docs/tool-reference.md](docs/tool-reference.md) / [简体中文](docs/tool-reference.zh.md) | Capability parameter reference (`py scripts/build_tool_reference.py` emits both) |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Add Capabilities, tests, packaging, release |
| [Resources/CapabilitySpec.md](Resources/CapabilitySpec.md) | Capability metadata spec |
| [Resources/AIRules.mdc](Resources/AIRules.mdc) | IDE Rule template (copy into the game project; [usage-guide §2.8](docs/usage-guide.md#28-挂载-airules)) |
| [CHANGELOG.md](CHANGELOG.md) | Version history |

## License

[MIT](LICENSE) © byteyang
