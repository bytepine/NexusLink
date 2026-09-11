# 为 NexusLink 做贡献

面向插件维护者与 Capability 作者。用户安装与四端接入见 [docs/usage-guide.md](docs/usage-guide.md)；架构见 [docs/architecture.md](docs/architecture.md)；代理会话层契约见 [docs/proxy-session.md](docs/proxy-session.md)。

## 新增 Tool / Capability

**路径 A — 纯 Tool**（轻量、无 section，直接重写 `ExecuteImpl`）

1. 创建 `Private/Tools/<模块>/NexusMcpToolXxx.h/.cpp`，继承 `FNexusMcpTool`
2. 实现 `GetName()` / `GetDescription()` / `ExecuteImpl()`
3. `.cpp` 末尾 `REGISTER_MCP_TOOL(FNexusMcpToolXxx)`

**路径 B — Capability**（主流路径）

1. 创建 `Private/Capabilities/<分类>/NexusXxxCapability.h/.cpp`；**先按 [CapabilitySpec.md](Resources/CapabilitySpec.md) §2.1.1 选基类**（`manage_*` + `operations[]` → `FNexusActionCapability`，子类禁止 `Execute`；`sections[]` → MultiSection；PIE → Runtime；其余 → `FNexusCapability`）
2. 实现该基类要求的钩子（Action：`BuildDefinition` + `RegisterActions` + `PrepareTarget`；普通：`BuildDefinition` + `Execute`）；资产 get/manage 须填 `Out.SearchAssetTypes`；`.cpp` 末尾 `REGISTER_MCP_CAPABILITY(...)`
3. 遵循 CapabilitySpec（命名 / 四段式描述 / 自检清单）
4. Capability 通过 `call_capability` 元工具直接调用，或在 MultiTool 模式下作为独立 MCP Tool 暴露

改 InputSchema 后：在编辑器跑 Automation `NexusLink.Smoke.PluginAndRegistry.SchemaDump`，更新 `scripts/generated/capability_schemas.json`，再跑 `py scripts/build_tool_reference.py`（优先读该 json；无文件时仍从 C++ 链抽取）。旧名表改 `Resources/legacy_capability_names.json` 后跑 `py scripts/gen_legacy_capability_names.py`。

新增或修改 Capability 后运行 `py scripts/build_tool_reference.py`，**同时**重生英文 [docs/tool-reference.md](docs/tool-reference.md) 与中文 [docs/tool-reference.zh.md](docs/tool-reference.zh.md)。不要只改其中一份生成段。新 cap 的中文 Description 补进 [scripts/tool_reference_zh.json](scripts/tool_reference_zh.json)；未收录的参数说明会按短语规则即时译成中文。

## 测试

两层自动化：

- **L0 跨版本编译**（宿主工程 `Script/build_test.py`）：UAT `BuildPlugin`。Editor 阶段按**该引擎** `Engine/Plugins` 探测编进可选插件 Capability（GAS/Niagara/ControlRig 等）；Game 阶段 `WITH_EDITOR=0` 仍不编这些 cap。

- **L1 C++ Automation**（`Source/NexusLinkTests/`）：纯工具函数 + 插件加载 + Capability 注册表冒烟 + `FNexusResponseCompactorUtils` 全量断言。经 UEEditor-Cmd 触发：

  ```bash
  UEEditor-Cmd YourProject.uproject -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi -NoSound -NoSplash
  ```

- **L2 pytest E2E**（在宿主游戏工程的 `Tests/` 自行维护）：经 `call_capability` 做端到端回归（SearchMode 下调用，不依赖 MultiTool）。日常默认 headless；发版按本次变更选模式（见下文「发版」）。

  ```powershell
  pip install -r Tests/requirements.txt
  python Script/run_e2e.py
  python Script/run_e2e.py --gui
  python Script/run_e2e.py --ue-url http://127.0.0.1:45000/stream
  ```

  报告输出到 `Saved/Logs/TestReport.xml`。详情见 [docs/testing.md](docs/testing.md)。

**新增 Capability 时**：在宿主工程 `Tests/test_*.py` 对应阶段文件中补至少一个 happy-path，使用 `client.call_capability("cap_name", {...})`。

## 本地打包

```bash
py scripts/build_unreal.py --version <version> --output release/
```

发行 zip **不含** `Source/NexusLinkTests`（L1 Automation 仅源码仓 / 开发构建）。

产物：`release/nexus-mcp-unreal-<version>.zip`（`EngineVersion: 4.26`，通用安装）。解压到 UE 项目 `Plugins/Developer/`。

## 发版（维护者）

GitHub Release **正文唯一来源**为 `CHANGELOG.md` 对应版本段落（CI 经 `scripts/extract_release_notes.py --verify` 提取）。禁止网页手写 Release 说明或 `gh release create`。

**L2 E2E 按本次变更选模式**（打 tag 前；日常默认仍是 headless，**不要**发版时一律 `--gui`）。判定材料：本仓 `[Unreleased]`、相对上一 tag 的 `git diff`、拟发版文件。在**宿主游戏工程**（含 `Tests/` 与 `Script/run_e2e.py`）执行：

| 本次发版内容 | 测试模式 | 命令 |
|---|---|---|
| 仅文档 / 发版脚本 / CHANGELOG（无 C++、cap、Tests） | 不强制 UE e2e | — |
| 仅编辑器资产 / manage-get / schema / 无 GUI 信号的 cap | Headless | `py Script/run_e2e.py` |
| 含 PIE / runtime Actor·Widget / UnLua / 视口 / RHI / `l4_runtime` / `lua` / `requires_gui` / `interact_runtime_*` / `spawn_runtime_*` / `control_pie` | GUI | `py Script/run_e2e.py --gui` |
| 混合（两者都有） | GUI（超集） | `py Script/run_e2e.py --gui` |
| 不确定 | 默认 GUI（CHANGELOG 已写 PIE / UnLua / viewport / `*_runtime_*` 时） | 同上 |

GUI 信号：cap 名含 `_runtime_`；标记 `l4_runtime` / `lua` / `requires_gui`；`interact_runtime_*` / `spawn_runtime_*` / `control_pie`；`capture_viewport` / `get_asset_texture` / `eval_runtime_lua` / `dofile_runtime_lua`。不要把模块 `Type=Runtime` / UncookedOnly 当成 GUI 信号。`--full` 与 `--gui` 等价。策略细节见 [docs/testing.md](docs/testing.md)。

**正式版**（`X.Y.Z`）：

1. 按上表跑完（或明确跳过）L2 E2E
2. 归档 `[Unreleased]` → `[X.Y.Z] - YYYY-MM-DD`，更新 `VERSION`
3. `py scripts/extract_release_notes.py --version X.Y.Z --verify`（预览 stdout，确认无误）
4. `git commit` → `git tag -a nexus-link-vX.Y.Z` → `git push origin HEAD` + `git push origin nexus-link-vX.Y.Z`

**Pre-release**（`X.Y.Z-beta.N`）：步骤同上，tag 为 `nexus-link-vX.Y.Z-beta.N`；CI 创建 GitHub **Pre-release**。

push tag 后 `.github/workflows/release.yml` 打包 `nexus-mcp-unreal-<ver>.zip` 并发布 Release。
