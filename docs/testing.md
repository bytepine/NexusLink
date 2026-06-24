# pytest E2E 测试指南

> 本指南描述在**宿主推 UE 游戏工程**中运行 NexusLink L2 回归测试。参考实现见 [NexusUnreal](https://github.com/bytepine/NexusUnreal) 的 `Tests/` 目录。


本目录是 NexusLink 的第二层测试（L2），通过 MCP HTTP 协议对 **3 个元工具 + 91 个 Capability**（`WITH_GAS=0` 时为 81）做真实端到端验证。第一层（L1，C++ Automation）跑 utility 单测，见 `Source/NexusLinkTests/`。

## 工具模型说明

NexusLink 默认工作在 **SearchMode**（仅暴露 3 个元工具）；全量回归时建议使用 **MultiTool** 模式（见 UE 插件设置）。

| 元工具 | 说明 |
|---|---|
| `search_capabilities` | 按关键词或精确名称查询 Capability |
| `call_capability` | 调用任意 Capability，参数嵌套在 `arguments` 字段 |
| `submit_feedback` | 向 NexusLink 提交反馈 |

### `MCPClient.call()` 路由行为

测试中的 `mcp.call("工具名", **kwargs)` 会自动路由：

- **元工具**（`search_capabilities` / `call_capability` / `submit_feedback`）：直接调用。
- **其他所有名称**：通过 `call_capability` 元工具转发，自动应用 `_framework/legacy_map.py` 中的历史名称映射。

推荐新测试使用 `mcp.call_capability("精确capability名", ...)` 明确指定 capability。

## 快速开始

```bash
pip install -r Tests/requirements.txt
```

### 模式 A：连现成的 UE Editor（开发常用）

启动 UE 项目 → 打开 Editor → 确认 NexusLink 加载 → 记下端口（默认 45000）。

```bash
pytest Tests --ue-url http://127.0.0.1:45000/stream
```

### 模式 B：Runner 自己拉 UEEditor-Cmd（CI）

```bash
pytest Tests \
    --ue-root "C:/Program Files/Epic Games/UE_5.4" \
    --uproject "Nexus.uproject"
```

或用 `run_e2e.py` 包装：

```bash
python Script/run_e2e.py --ue-url http://127.0.0.1:45000/stream
```

## 常用过滤

| 命令 | 用途 |
|---|---|
| `pytest Tests -m "not l4_runtime"` | 跳过需要 PIE 的用例 |
| `pytest Tests -m "not lua"` | 跳过 UnLua 相关用例 |
| `pytest Tests -k blueprint` | 只跑蓝图相关用例 |
| `pytest Tests --keep-artifacts` | 保留 `/Game/_McpTest/<ts>/` 方便诊断 |

## 覆盖映射

Tests 目录文件与原 `TEST_CHECKLIST.md` 阶段一一对应：

| 阶段 | 测试文件 |
|---|---|
| 一：基础探测 + 二：控制台命令 | `test_00_smoke.py`、`test_10_editor.py` |
| 三：Struct + DataTable | `test_20_struct_datatable.py` |
| 四：Blueprint + Graph | `test_30_blueprint.py` |
| 五：Widget Blueprint | `test_40_widget.py` |
| 六：Material | `test_50_material.py` |
| 七：资产引用 | `test_60_asset_refs.py` |
| 八：Gameplay Tags | `test_70_gameplay_tags.py` |
| 九：AI + 动画资产 | `test_80_ai_anim_assets.py` |
| 十：资产管理 | `test_90_asset_mgmt.py` |
| 十一：PIE Runtime（含 Lua） | `test_95_pie_runtime.py` |
| 十二：清理 + 日志健康 | session 级 fixture + `test_90_asset_mgmt.py` 末尾 |

## 约束

- 写入测试**必须**走 `test_ns` fixture 得到的 `/Game/_McpTest/<ts>/` 命名空间，禁碰业务资产。
- 任意单工具失败不影响其他用例；用例间不要共享可变状态（除 session 级 fixture）。
- 新增 Capability → 同步在对应 `test_*.py` 写至少一个 happy-path 用例，并更新 `TEST_CHECKLIST.md`。
- 全量回归测试依赖 NexusWork 工作区的 `mcp-regression-test` skill（需实机 UE）。
