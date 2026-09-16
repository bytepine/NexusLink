**Language / Language**: [简体中文](README.md) · **English**

# NexusLink — UE MCP Plugin

An MCP integration plugin for Unreal Engine that exposes UE project context to AI tools via the MCP protocol.

> Supports UE 4.26 and all later versions (including UE5). Split into two modules: `NexusLink` (`Type: Runtime`; standalone Development/DebugGame Game/DS builds can host MCP too) + `NexusLinkEditor` (`Type: Editor`; editor UI). **Shipping builds strip the MCP server entirely at compile time.**

> **Upgrading from 1.x**: 2.0 enables auth by default and switches all MCP/AI-facing text to English. Upgrade the plugin and clients to 2.x together — see [usage-guide §0](docs/usage-guide.md#0-从-1x-升级到-20) (Chinese).

## 5-minute quick start (NexusDesktop recommended)

For daily use, the recommended relay is [NexusDesktop](https://github.com/bytepine/NexusDesktop): no IDE extension, a stable endpoint, and instance switching when multiple UE processes are running.

1. Download `nexus-mcp-unreal-<version>.zip` from [NexusLink Releases](https://github.com/bytepine/NexusLink/releases) and extract it to `Plugins/NexusLink`
2. Enable **Edit → Plugins → Developer → NexusLink**, then restart the editor
3. Open **Edit → Editor Preferences → Plugins → NexusLink**, check **Enable MCP Server** (off by default), and verify the runtime status and title-bar MCP / WS ports
4. Download the latest stable [NexusDesktop release](https://github.com/bytepine/NexusDesktop/releases): use `*-setup.exe` on Windows or `.dmg` on macOS; `*-update.zip` is not an installer
5. Start NexusDesktop, check **Enable relay server** in the tray (default `:6700`), and verify that the status line shows the current UE project
6. Choose **MCP client configuration…** in the tray, select **Streamable HTTP** and Cursor / CodeBuddy, then paste the generated configuration into the AI client
7. Restart the MCP session. Default SearchMode should expose 3 meta-tools: `search_capabilities`, `call_capability`, and `submit_feedback`

> **Security**: MCP auth is on by default. Treat the token as a credential: never commit it, paste it into public documentation, or expose it in screenshots. Review version-control changes before committing because write calls modify real assets or runtime state.

When the UE-side server is disabled, the title bar shows no port, NexusDesktop cannot discover the instance, and direct `http://127.0.0.1:45000/stream` connections get no response. For full setup, auth, and remote-host instructions, see the [usage guide](docs/usage-guide.md) (Chinese).

## Other connection options

NexusLink serves HTTP `:45000` + WebSocket `:55000`. Run only one local proxy so multiple proxies do not scan and connect to the same UE process.

| Client | MCP endpoint | Notes |
|--------|--------------|-------|
| **[NexusDesktop](https://github.com/bytepine/NexusDesktop)** (recommended) | `http://127.0.0.1:6700/stream` | Standalone tray app; no IDE extension |
| **[NexusRider](https://github.com/bytepine/NexusRider)** | `http://127.0.0.1:6800/stream` | Rider Marketplace: **Nexus MCP** |
| **[NexusVSCode](https://github.com/bytepine/NexusVSCode)** | `http://127.0.0.1:6900/stream` | VSCode / Cursor / CodeBuddy / Windsurf extension |
| Direct UE | `http://127.0.0.1:45000/stream` | No proxy cache, write gate, or instance switching |

## Runtime / Dedicated Server debugging

- Editor and PIE can use the Preferences switch. Development / DebugGame standalone Game builds, `-game` / `-server` child processes, and commandlets do not read that switch; explicitly append `-EnableNexusMcp` to their launch arguments
- If another UE process owns the default ports, the server advances to the next available ports; select the intended instance in NexusDesktop
- PIE and standalone Game can use `NexusLink.Mcp panel` for status and session-only Capability toggles. Dedicated Server has no viewport; use `NexusLink.Mcp status`
- Runtime Capabilities cover logs, Actor properties, animation, behavior trees, GAS, widgets, Lua, and more. Dedicated Server has no viewport or UMG
- Shipping builds strip MCP at compile time

GAS / Niagara Capabilities are detected from the host project; NexusLink does **not** force those plugins via `.uplugin`.

## Example project

Public sample [NexusUnreal](https://github.com/bytepine/NexusUnreal) (ThirdPerson template + UnLua + MCP regression tests). The plugin is a git submodule and **is not bundled** with the sample; clone with `--recurse-submodules` or install this plugin separately.

## Coverage

Default **SearchMode**: `tools/list` exposes 3 meta-tools (`search_capabilities` / `call_capability` / `submit_feedback`); Capabilities are discovered on demand. Coverage includes editor, Blueprint, animation, material, audio, AI / EQS, GAS, UMG, Niagara, UnLua, and PIE / standalone Game / Dedicated Server runtime debugging. Full parameters: [docs/tool-reference.md](docs/tool-reference.md) ([简体中文](docs/tool-reference.zh.md)). SearchMode vs MultiTool: [docs/architecture.md](docs/architecture.md#暴露模式toolslistmode).

## Dangerous Capabilities (all disabled by default)

Four "script escape hatch" capabilities carry the `dangerous` tag. **Editor Preferences → Plugins → NexusLink → Dangerous Capability → Access mode**:

| Mode | Behavior |
|---|---|
| **Disabled** (default) | Search reports `disabled`, calls fail. Tree keeps your checkmarks but greys them out (not editable) |
| **Confirm each request** | Keeps existing checkmarks; you can change them. Checked caps are discoverable and callable; the editor prompts before each run. The AI must pass `reason` (purpose, expected effect, why no safer dedicated cap). Allow applies to **this call only**; deny returns `errorKind=user_denied` (do not retry). Timeout (default 90s) auto-denies. Exit immersive PIE if the window is hidden |
| **Custom** | Per-cap checkboxes in the Capability tree; checked = always allow, no prompt |

Launch with `-NexusEnableDangerousCaps` still session-enables all four (never written to settings) and overrides the access mode. Caps you enabled by hand are not overwritten by later upgrades; if any dangerous cap is already enabled, upgrade migrates to Custom.

| Capability | What it does | Extra requirement |
|---|---|---|
| `exec_command` | Run a UE console command and capture its output | — |
| `exec_python` | Run Python in the editor (`exec` / `file` / `eval`); returns stdout and traceback | Python Editor Script Plugin enabled in the host project |
| `eval_runtime_lua` | Evaluate a Lua snippet in PIE/Game; returns stacked values | UnLua + PIE |
| `dofile_runtime_lua` | Load and run a `.lua` file from `Content/Script/` | UnLua + PIE |

> Read-only probing goes through **`get_python_api`**. It needs the same Python plugin but only runs `inspect`, embedding whitelist-validated arguments into a fixed script — it never accepts user code, so it is **enabled by default**. Use it to check whether an `unreal.*` API exists on this engine version instead of turning on `exec_python`.

### Why they are off by default

- **Equivalent to arbitrary in-process code execution**: Python / Lua can `import os`, touch any file, and spawn subprocesses. Once enabled, auth is the only remaining boundary and per-capability enable/disable stops meaning anything.
- **Only half the write-path safety net applies**: every other Capability wraps its writes in `FNexusEditorTransaction` (undoable; a failed `calls[]` batch rolls back as a whole). `exec_python` opens a transaction too, but UE only records objects that went through `Modify()`: the most natural Python form, `obj.foo = x`, uses `NotifyMode::Never` and is **not** recorded — only `obj.set_editor_property(...)` and an explicit `obj.modify()` are. A script mixing both leaves Ctrl+Z rolling back half the change, landing the asset in a state that never existed. The returned `undoRecorded` reports honestly whether this call produced any undoable record. Package-level operations (`create_asset` / `delete_asset` / `save_asset`) are outside transactions entirely.
- **They crash the editor easily**: model-written scripts routinely hit the wrong thread, stale objects, or GC — and the crash is hard to attribute afterwards.

### Trade-off versus the default-enabled Capabilities

| Aspect | Declarative Capability (default on) | Script escape hatch (default off) |
|---|---|---|
| Coverage | Only the domains already implemented | Whatever the engine exposes — full long tail |
| Arguments | JSON Schema validated; bad args fail fast with `arg_invalid` | Free-form text; errors surface as a runtime traceback |
| Response | Structured + `*_defaults` compaction, predictable token cost | Unstructured stdout, easy to blow up the response body |
| Undo | Uniform transaction wrapper | Only the `set_editor_property` / `modify()` path; direct assignment and package ops are not — partial rollback is easy to hit |
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
| [Resources/AIRules.mdc](Resources/AIRules.mdc) | IDE Rule template (copy into the game project; [usage-guide §2.9](docs/usage-guide.md#29-挂载-airules)) |
| [CHANGELOG.md](CHANGELOG.md) | Version history |

## License

[MIT](LICENSE) © byteyang
