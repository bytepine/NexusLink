<a id="pytest-e2e-测试指南"></a>
# pytest E2E testing guide
[中文](testing.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

> This guide describes running NexusLink L2 regression tests in a **host UE game project**. Create Tests/, Script/run_e2e.py, and other needed directories in that project; this repository supplies plugin source only, without an example test project.

L2 performs real end-to-end checks of the three meta-tools and registered Capabilities over MCP HTTP; host plugin gates trim the set. L1 C++ Automation belongs in the host plugin Plugins/NexusLinkTestSuite/, **outside this repository**.

<a id="工具模型说明"></a>
## Tool model

NexusLink defaults to **SearchMode**, exposing only three meta-tools. **MultiTool** mode is recommended for full regression (UE plugin settings).

| Meta-tool | Description |
|---|---|
| SearchCapabilities | Query Capabilities by keywords or exact name |
| CallCapability | Call any Capability with parameters nested under Arguments |
| SubmitFeedback | Submit feedback to NexusLink |

<a id="mcpclientcall-路由行为"></a>
### MCPClient.call() routing

In tests, mcp.call("tool-name", **kwargs) routes automatically:

- **Meta-tools** SearchCapabilities / CallCapability / SubmitFeedback call directly.
- **All other names** forward through CallCapability with legacy mappings from _framework/legacy_map.py.

For new tests, prefer mcp.CallCapability("ExactCapabilityName", ...) to specify the capability explicitly.

<a id="快速开始"></a>
## Quick start

```bash
pip install -r Tests/requirements.txt
```

<a id="模式-a连现成的-ue-editor开发常用"></a>
### Mode A: connect to an existing UE Editor (usual development setup)

Launch the UE project, open Editor, confirm NexusLink is loaded, and note the port (default 45000).

```bash
pytest Tests --ue-url http://127.0.0.1:45000/stream
```

<a id="模式-brunner-自己拉-ueeditor-cmdci"></a>
### Mode B: the runner starts UEEditor-Cmd (CI)

```bash
pytest Tests \
    --ue-root "C:/Program Files/Epic Games/UE_5.4" \
    --uproject "/path/to/YourProject.uproject"
```

Or use the run_e2e.py wrapper:

```bash
python Script/run_e2e.py --ue-url http://127.0.0.1:45000/stream
```

<a id="常用过滤"></a>
## Common filters

| Command | Purpose |
|---|---|
| pytest Tests -m "not l4_runtime" | Skip PIE-dependent cases |
| pytest Tests -m "not lua" | Skip host NexusLinkExt Lua cases |
| pytest Tests -k blueprint | Run Blueprint cases only |
| pytest Tests --keep-artifacts | Retain /Game/_McpTest/<ts>/ for diagnosis |

<a id="覆盖映射"></a>
## Coverage map

Organize Tests/test_*.py by feature domain, corresponding to these phases:

| Phase | Test file |
|---|---|
| 1: basic probes + 2: console commands | test_00_smoke.py, test_10_editor.py |
| 3: Struct + DataTable | test_20_struct_datatable.py |
| 4: Blueprint + Graph | test_30_blueprint.py |
| 5: Widget Blueprint | test_40_widget.py |
| 6: Material | test_50_material.py |
| 7: asset references | test_60_asset_refs.py |
| 8: Gameplay Tags | test_70_gameplay_tags.py |
| 9: AI + animation assets | test_80_ai_anim_assets.py |
| 10: asset management | test_90_asset_mgmt.py |
| 11: PIE Runtime, including NexusLinkExt Lua | test_95_pie_runtime.py |
| 12: cleanup + log health | session fixtures + the end of test_90_asset_mgmt.py |

<a id="约束"></a>
## Constraints

- Write tests **must** use the /Game/_McpTest/<ts>/ namespace supplied by test_ns; never touch gameplay assets.
- One tool failure must not affect other cases. Avoid sharing mutable state between cases except session fixtures.
- Every added Capability needs at least one happy-path case in its corresponding test_*.py.
- **Default verification:** host py Script/run_e2e.py, headless and fast. Do not change the daily/CI default to GUI.
- **CLI cannot cover** PIE / NexusLinkExt Lua / viewport-RHI: mark these cases l4_runtime / lua / requires_gui and pass them with py Script/run_e2e.py --gui.
- **Full regression:** --gui or --full runs GUI Editor, including those marked cases.
- **Release verification:** choose headless or --gui according to the current changes, using [the CONTRIBUTING release table](../CONTRIBUTING.en.md), rather than always GUI.
