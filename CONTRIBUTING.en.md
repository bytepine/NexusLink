# Contributing to NexusLink

**语言 / Language**: [简体中文](CONTRIBUTING.md) · **English**

[Independent engine backends](docs/toolset-backends.en.md) describes direct engine API implementations and version adaptation. NexusLink itself must not directly link to or call official Toolset, ToolsetCore, or ToolsetRegistry modules, or import official Python toolset packages. Engine and domain plugins may retain their own transitive dependencies; see that document for the boundary.

[Animation and domain actions](docs/animation-niagara-gamefeatures-toolsets.en.md) covers Sequencer / Control Rig, deep Niagara writes, Game Features, and NexusLink-owned action discovery.

[Documentation index](docs/README.en.md) · [Project home](README.en.md)

Capabilities for the engine itself and engine-bundled domain plugins belong in `NexusLink` / `NexusLinkEditor`. Only third-party plugin capabilities belong in `NexusLinkExt` / `NexusLinkExtEditor`. Optional enablement does not make an engine plugin third-party.

This guide is for plugin maintainers and Capability authors. For installation and all four connection routes, see [the usage guide](docs/usage-guide.en.md); also see [architecture](docs/architecture.en.md) and [the proxy session contract](docs/proxy-session.en.md).

## Documentation languages

All maintained project documentation must provide equivalent Chinese and English content, in separate paired Chinese and English files with reciprocal language links at the top. This includes README, guides, specifications, contribution and testing instructions, and every CHANGELOG entry. Update both languages in the same change; titles or summaries alone do not count as a translation. Preserve commands, identifiers, version headings, and link targets; regenerate both languages of generated documentation from their sources.

## Adding a Tool / Capability

For the planned UMG, material, configuration, and Sequencer interface scope, see [the follow-up guide](docs/editor-remaining-toolsets.en.md).

Before extending editor operations, check [independent interface scope](docs/editor-compatibility.en.md). Reuse NexusLink implementations or call public engine APIs directly; do not route through official Toolsets. Version adaptations need read/write round trips and invalid-input checks, explicit minimum versions, and restricted-action declarations. Structured Blueprint plans connect only nodes created by that plan, enabling cleanup after failure.

**Route A — plain Tool** (lightweight, no sections; override `ExecuteImpl` directly)

1. Create `Private/Tools/<module>/NexusMcpToolXxx.h/.cpp`, inheriting `FNexusMcpTool`.
2. Implement `GetName()` / `GetDescription()` / `ExecuteImpl()`.
3. Add `REGISTER_MCP_TOOL(FNexusMcpToolXxx)` at the end of the `.cpp`.

**Route B — Capability** (the usual route)

1. **Select the module first using [CapabilitySpec](Resources/CapabilitySpec.en.md) §2.1.0.** Third-party plugin capabilities, currently UnLua, belong in [NexusLinkExt](https://github.com/bytepine/NexusLinkExt) under §2.1.2. Within this plugin, live PIE/Game objects without disk saving belong in Runtime `NexusLink`; disk assets, editor APIs, and disk saving belong in `NexusLinkEditor`. Then **select the base class** under §2.1.1: `Manage*` + `Operations[]` uses `FNexusActionCapability`, whose subclasses must not implement `Execute`; `Sections[]` uses MultiSection; PIE/Game runtime capabilities including `GetOutputLog` / `ExecCommand` / `CaptureViewport` use `FNexusRuntimeCapability`; otherwise use `FNexusCapability`. Create `Source/<module>/Private/Capabilities/<category>/NexusXxxCapability.h/.cpp`, with class name `FNexusXxxCapability`. CapabilitySpec §6 and §8.3 define naming: tool IDs/fields use PascalCase and variable names express their roles.
2. Implement the base-class hooks: Action uses `BuildDefinition` + `RegisterActions` + `PrepareTarget`; ordinary capabilities use `BuildDefinition` + `Execute`. Asset get/manage capabilities must fill `Out.SearchAssetTypes`. End the `.cpp` with `REGISTER_MCP_CAPABILITY(...)`.
3. Follow CapabilitySpec naming, four-part descriptions, and self-checks.
4. Invoke the Capability through `CallCapability`, or expose it as an individual MCP Tool in MultiTool mode.

After changing InputSchema, run editor Automation `NexusLink.Smoke.PluginAndRegistry.SchemaDump`, which exports to the host's `Saved/NexusLink/capability-schemas.json` by default. Compare it against reviewed `scripts/contracts/input-schemas.json` with `py scripts/check_schema_compatibility.py <exported-file>`. For specific host/optional-plugin combinations, pass `--enabled <reviewed-capability-name-array.json>`; never derive that reviewed set automatically from the current export. Tests do not overwrite the baseline. After confirming compatibility, explicitly update `scripts/generated/capability_schemas.json`, optionally by launching with `-NexusSchemaOutput=<absolute-output-path>`, then run `py scripts/build_tool_reference.py`. After changing `Resources/legacy_capability_names.json`, run `py scripts/gen_legacy_capability_names.py`.

After adding or changing a Capability, run `py scripts/build_tool_reference.py` to regenerate **both** [English](docs/tool-reference.md) and [Chinese](docs/tool-reference.zh.md) references. Do not edit only one generated section. Add Chinese descriptions for new capabilities to [scripts/tool_reference_zh.json](scripts/tool_reference_zh.json); parameter descriptions not explicitly listed are translated through phrase rules during generation. After adding/removing capabilities, update `EXPECTED_CAP_COUNT` in `scripts/audit_capability_naming.py`.

Plugin script checks require no UE. The host's `run_e2e.py` also runs the first two as prerequisites:

```bash
py scripts/audit_capability_params.py
py scripts/audit_capability_naming.py
py scripts/audit_doc_sync.py   # Report: source vs tool-reference / Chinese overlay
py scripts/audit_cpp_naming.py  # Report: type prefixes/filenames, b booleans, k constants, PascalCase protocol
py -m pytest scripts/tests/test_no_toolset_dependencies.py  # Zero direct Toolset dependencies/calls
```

When Windows binaries are available, run `py scripts/audit_binary_dependencies.py <NexusLink.dll> <NexusLinkEditor.dll>` from a VS Native Tools prompt, or specify the tool with `--dumpbin <path>`. Neither regular nor delayed imports may contain official Toolset DLLs; inability to inspect counts as failure. Transitive dependencies internal to the official engine are outside this plugin boundary. Retain the precise [acceptance definition](docs/toolset-backends.en.md) with the validation report.

## Testing

Automation layers:

- **L0 cross-version builds** (host `Script/build_test.py`): build the entire `Nexus.uproject` for every locally installed engine, including `NexusEditor` and `Nexus` Game, game modules, and project plugins such as NexusLink/NexusLinkExt. Isolate Intermediate in a temporary directory per engine without modifying repository project files. Build one engine at a time; do not pass `-MaxParallelActions`, leaving parallelism to UBT.
- **L1 C++ Automation** (in the host, **not this repository**): `Plugins/NexusLinkTestSuite/` covers this plugin; UnLua capabilities live in `Plugins/NexusLinkExtTestSuite/`. Trigger through UEEditor-Cmd:

  ```bash
  UEEditor-Cmd YourProject.uproject -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi -NoSound -NoSplash
  ```

- **L2 pytest E2E** (maintained in the host game's `Tests/`): run end-to-end regression through `CallCapability` in SearchMode, independent of MultiTool. Daily runs are headless by default; select release mode according to the changes, as described below.

  ```powershell
  pip install -r Tests/requirements.txt
  python Script/run_e2e.py
  python Script/run_e2e.py --gui
  python Script/run_e2e.py --ue-url http://127.0.0.1:45000/stream
  ```

  Reports go to `Saved/Logs/TestReport.xml`. See [testing](docs/testing.en.md).

**For every new Capability**, add at least one happy-path case to the corresponding host `Tests/test_*.py` phase file, using `client.CallCapability("cap_name", {...})`.

## Local packaging

```bash
py scripts/build_unreal.py --version <version> --output release/
```

The release ZIP contains the plugin itself. L1 Automation lives in the example project's `NexusLinkTestSuite`, outside this repository.

Output: `release/nexus-mcp-unreal-<version>.zip`, a universal installation with `EngineVersion: 4.26`. Extract to a UE project's `Plugins/`; the ZIP's top-level directory is `NexusLink/`.

## Releases (maintainers)

The GAS retest tools under `tools/gas_retest` use only the Python standard library. After changes, run `python -m unittest gas_retest.test_runner gas_retest.test_report gas_retest.test_compare` from `tools/`, as Build CI also does; packaging regression uses `python -m pytest scripts/tests`. Baseline/comparison operations are offline. Regression covers new failures, recovery, missing steps, event deduplication/session noise, case signatures, and file protection. Installation packages exclude all of `tools/`, including tools, tests, examples, and local reports. Obtain command-line tools from the matching source version and run them under the source repository's `tools/`. Online validation uses the public example project's `Script/gas_network_retest.py`; fixed cases and baseline gates/archival use `Script/gas_regression.py`. Changes to fixed cases or signature algorithms require manual review of the example-project baseline; never automatically accept current results. See [GAS diagnostics and retesting](docs/gas-retest.en.md) for formats and limits.

The **only source of GitHub Release notes** is the matching `CHANGELOG.md` version section, extracted by CI through `scripts/extract_release_notes.py --verify`. Do not handwrite notes on GitHub or use `gh release create`.

**Choose L2 E2E mode from the release changes** before tagging. Daily runs remain headless by default; do not blindly use `--gui` for every release. Review `[Unreleased]`, the diff from the previous tag, and proposed release files. Run inside the **host game project**, which contains `Tests/` and `Script/run_e2e.py`:

| Release contents | Test mode | Command |
|---|---|---|
| Documentation / release scripts / CHANGELOG only, with no C++, capabilities, or Tests | UE E2E not mandatory | — |
| Editor assets / manage-get / schemas / capabilities without GUI signals only | Headless | `py Script/run_e2e.py` |
| PIE / runtime Actors or Widgets / NexusLinkExt Lua / viewport / RHI / `l4_runtime` / `lua` / `requires_gui` / `interact_runtime_*` / `spawn_runtime_*` / `ControlPie` | GUI | `py Script/run_e2e.py --gui` |
| Mixed contents | GUI, the superset | `py Script/run_e2e.py --gui` |
| Uncertain, with PIE / NexusLinkExt / viewport / `*_runtime_*` already in CHANGELOG | Default to GUI | Same as above |

GUI signals include capability names containing `_runtime_`; markers `l4_runtime` / `lua` / `requires_gui`; `interact_runtime_*` / `spawn_runtime_*` / `ControlPie`; and `CaptureViewport` / `CaptureEditorPanel` / `GetAssetTexture`. `lua`, `EvalRuntimeLua`, and `DofileRuntimeLua` belong to [NexusLinkExt](https://github.com/bytepine/NexusLinkExt), outside this repository. A capability's module (`NexusLink` Runtime or `NexusLinkEditor` Editor) alone is not a GUI signal. `--full` is equivalent to `--gui`. See [testing policy](docs/testing.en.md).

**Stable Release** (`X.Y.Z`):

1. Run L2 E2E using the table above, or explicitly record why it is skipped.
2. Archive `[Unreleased]` as `[X.Y.Z] - YYYY-MM-DD` and update `VERSION`. If `[X.Y.Z-beta.N]` sections exist, deduplicate their user-visible capabilities into `[X.Y.Z]` using short descriptions of final behavior; omit fixes to features newly introduced in this release. GitHub's stable Release displays only this section; retain beta history.
3. Preview and verify with `py scripts/extract_release_notes.py --version X.Y.Z --verify`.
4. `git commit` → `git tag -a nexus-link-vX.Y.Z` → `git push origin HEAD` and `git push origin nexus-link-vX.Y.Z`.

**Pre-release** (`X.Y.Z-beta.N`): archive only the current `[Unreleased]` increment without copying earlier betas again. Use tag `nexus-link-vX.Y.Z-beta.N`; CI creates a GitHub **Pre-release**.

After pushing the tag, `.github/workflows/release.yml` packages `nexus-mcp-unreal-<ver>.zip` and publishes the Release.
