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

- **L1 C++ Automation**（`Source/NexusLinkTests/`）：纯工具函数 + 插件加载 + Capability 注册表冒烟 + `FNexusResponseCompactorUtils` 全量断言。经 UEEditor-Cmd 触发：

  ```bash
  UEEditor-Cmd YourProject.uproject -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi -NoSound -NoSplash
  ```

- **L2 pytest E2E**（在宿主游戏工程的 `Tests/` 自行维护）：经 `call_capability` 做端到端回归（SearchMode 下调用，不依赖 MultiTool）：

  ```powershell
  pip install -r Tests/requirements.txt
  python Script/run_e2e.py --ue-url http://127.0.0.1:45000/stream
  ```

  报告输出到 `Saved/Logs/TestReport.xml`。详情见 [docs/testing.md](docs/testing.md)。

**新增 Capability 时**：在宿主工程 `Tests/test_*.py` 对应阶段文件中补至少一个 happy-path，使用 `client.call_capability("cap_name", {...})`。

## 本地打包

```bash
py scripts/build_unreal.py --version <version> --output release/
```

发行 zip **不含** `Source/NexusLinkTests`（L1 Automation 仅源码仓 / 开发构建）。

产物：`release/nexus-mcp-unreal-<version>.zip`（`EngineVersion: 4.26`，通用安装）；发版另附 `nexus-mcp-unreal-<version>-ue5.8.zip`（Fab / UE 5.8 专用）。解压到 UE 项目 `Plugins/Developer/`。

## 发版（维护者）

GitHub Release **正文唯一来源**为 `CHANGELOG.md` 对应版本段落（CI 经 `scripts/extract_release_notes.py --verify` 提取）。禁止网页手写 Release 说明或 `gh release create`。

**正式版**（`X.Y.Z`）：

1. 归档 `[Unreleased]` → `[X.Y.Z] - YYYY-MM-DD`，更新 `VERSION`
2. `py scripts/extract_release_notes.py --version X.Y.Z --verify`（预览 stdout，确认无误）
3. `git commit` → `git tag -a nexus-link-vX.Y.Z` → `git push origin HEAD` + `git push origin nexus-link-vX.Y.Z`

**Pre-release**（`X.Y.Z-beta.N`）：步骤同上，tag 为 `nexus-link-vX.Y.Z-beta.N`；CI 创建 GitHub **Pre-release**。

push tag 后 `.github/workflows/release.yml` 打包 `nexus-mcp-unreal-<ver>.zip` 与 `nexus-mcp-unreal-<ver>-ue5.8.zip` 并发布 Release。
