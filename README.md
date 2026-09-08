# NexusLink — UE MCP 插件

**语言 / Language**: **简体中文** · [English](README.en.md)

基于 Unreal Engine 的 MCP 集成插件，将 UE 项目上下文通过 MCP 协议暴露给 AI 工具。

> 支持 UE 4.26 及以上所有版本（含 UE5）。主模块 `Type: UncookedOnly`，**MCP 跑在 Editor 二进制（含 `-server`/`-game`）**；cooked 包不包含本模块。

> **从 1.x 升级**：2.0 默认开启鉴权且 MCP/AI 可见文案改为英文，插件与客户端须同批升到 2.x。见 [usage-guide §0](docs/usage-guide.md#0-从-1x-升级到-20)。

## 安装与启用

从 [NexusLink Releases](https://github.com/bytepine/NexusLink/releases) 下载 `nexus-mcp-unreal-<version>.zip`，或克隆本仓库到项目的 `Plugins/Developer/NexusLink`。

1. 将插件放入 `Plugins/Developer/NexusLink`，在 **Edit → Plugins → Developer → NexusLink** 中启用并重启编辑器
2. **Edit → Editor Preferences → Plugins → NexusLink** — 勾选 **启用 MCP 服务器**（**默认关闭**）。勾选后即时启动 HTTP（`POST /stream`）与 WebSocket；取消勾选立即停止。**MCP 鉴权**默认开。Token、多机、开关组合见 [usage-guide §1.1](docs/usage-guide.md#11-鉴权)。默认仅本机 loopback；跨机再勾选 **允许局域网绑定**，用 **复制跨机连接** 选网卡 IP。
3. （可选）无 UI 的编辑器启动（如 `UEEditor-Cmd`）可加 **`-EnableNexusMcp`** 或控制台 **`NexusLink.EnableMcp 1|0`**（会话级，不写盘；与 Preferences 为 OR）。测试可加 **`-NexusEnableDangerousCaps`** 打开 `exec_command` / `eval_runtime_lua` / `dofile_runtime_lua` / `exec_python`

GAS / Niagara 等 Capability 按宿主项目插件探测，NexusLink **不**在 `.uplugin` 里强制依赖。

未启用时：标题栏不显示端口、客户端扫描不到实例、直连 `http://127.0.0.1:45000/stream` 无响应。完整步骤见 [docs/usage-guide.md](docs/usage-guide.md)。

## 接入 AI 客户端

NexusLink 提供 HTTP `:45000` + WebSocket `:55000`。日常推荐经客户端代理（固定端口、多实例切换）；也可直连 UE。四端端口与开关层数见 [usage-guide §1](docs/usage-guide.md)。

| 客户端 | 端点 | 说明 |
|--------|------|------|
| **[NexusDesktop](https://github.com/bytepine/NexusDesktop)** | `:6700` | 独立托盘程序；Windows `Setup.exe` / macOS `.dmg`（不要下载 `*-update.zip`） |
| **[NexusRider](https://github.com/bytepine/NexusRider)** | `:6800` | Rider Marketplace 搜索 **Nexus MCP** |
| **[NexusVSCode](https://github.com/bytepine/NexusVSCode)** | `:6900` | 扩展商店搜索 **Nexus MCP** |
| 直连 UE | `:45000` | 不用代理；须自行指定 UE 端口 |

## 示例工程

公开示例 [NexusUnreal](https://github.com/bytepine/NexusUnreal)（ThirdPerson 模板 + UnLua + MCP 回归测试）。插件以子模块挂载，**不随示例仓分发**；克隆须 `--recurse-submodules` 或单独安装本插件。

## 能力范围

默认 **SearchMode**：`tools/list` 仅 3 个元工具（`search_capabilities` / `call_capability` / `submit_feedback`），按需发现 Capability。覆盖编辑器、蓝图、动画、材质、音频、AI / EQS、GAS、控件、Niagara、PIE 运行时、UnLua 等。完整参数见 [docs/tool-reference.zh.md](docs/tool-reference.zh.md)（[English](docs/tool-reference.md)）；SearchMode vs MultiTool 见 [docs/architecture.md](docs/architecture.md#暴露模式toolslistmode)。

## 危险 Capability（默认禁用）

四个「脚本逃生舱」能力，安装或升级时按名写入禁用列表，**默认调不到**。需要时在 **Editor Preferences → Plugins → NexusLink** 里逐个勾选，或启动加 `-NexusEnableDangerousCaps`（会话级，不写盘）。手动勾选过的不会被后续升级覆盖。

| Capability | 做什么 | 附加前提 |
|---|---|---|
| `exec_command` | 执行 UE 控制台命令并捕获输出 | — |
| `exec_python` | 编辑器内执行 Python（`exec` / `file` / `eval`），回 stdout 与 traceback | 宿主启用 Python Editor Script Plugin |
| `eval_runtime_lua` | 在 PIE/Game 执行 Lua 片段，返回压栈值 | UnLua + PIE |
| `dofile_runtime_lua` | 从 `Content/Script/` 加载执行 `.lua` | UnLua + PIE |

> 只读探测走 **`get_python_api`**：它同样需要 Python Editor Script Plugin，但只做 `inspect`，参数经白名单校验后嵌入固定脚本，不接受用户代码，因此**默认开启**。想知道某个 `unreal.*` API 在本引擎版本上存不存在，用它，不必为此打开 `exec_python`。

### 为什么默认关

- **等价于进程内任意代码执行**：Python / Lua 可 `import os`、读写任意文件、起子进程。一旦开启，鉴权就成了唯一防线，按 Capability 的启用/禁用粒度全部失效。
- **写路径的安全网只剩半张**：其余 Capability 的写操作统一包 `FNexusEditorTransaction`（可 Undo，`calls[]` 批量失败整体回滚）。`exec_python` 同样开了事务，但 UE 只记录调用过 `Modify()` 的对象：Python 里最自然的 `obj.foo = x` 走 `NotifyMode::Never`，**不进事务**；只有 `obj.set_editor_property(...)` 与显式 `obj.modify()` 会。脚本混用两种写法时 Ctrl+Z 只回滚一半，资产会落进一个从未存在过的中间态。返回里的 `undoRecorded` 如实回报本次是否产生了可回滚记录。此外 `create_asset` / `delete_asset` / `save_asset` 等包级操作本来就不在事务范围内。
- **容易崩编辑器**：模型现写的脚本很容易碰到错误线程、失效对象或 GC，崩溃后也难归因。

### 与默认开启的 Capability 的取舍

| 维度 | 声明式 Capability（默认开） | 脚本逃生舱（默认关） |
|---|---|---|
| 覆盖面 | 覆盖已实现的域，没做的做不了 | 引擎暴露多少就能做多少，长尾全覆盖 |
| 参数 | JSON Schema 校验，错参立即 `arg_invalid` | 自由文本，错误要等运行期 traceback |
| 返回 | 结构化 + `*_defaults` 压缩，token 可控 | 非结构化 stdout，容易打爆响应体 |
| 可撤销 | 统一事务包裹 | 仅 `set_editor_property` / `modify()` 路径可撤；直接赋值与包级操作不可，易半回滚 |
| 控制流 | `calls[]` 只能批量，无条件与循环 | 任意分支循环，一次往返做完 |
| 跨版本 | `NX_*` 语义宏在编译期消化 4.26~5.8 差异 | 由脚本自己承担，UE4/UE5 API 差异易翻车 |

### 什么时候才用

先 `search_capabilities` 找专用 cap。只有三种情况值得开逃生舱：目标能力确实**没有**对应 Capability（顺手 `submit_feedback` 提一条）；需要一段带条件或循环的批处理；排查某个 cap 自身的 bug。用完建议关回去。

## 文档

| 文档 | 受众 |
|------|------|
| [docs/usage-guide.md](docs/usage-guide.md) | 安装、开关层、**鉴权**、四端接入 |
| [docs/architecture.md](docs/architecture.md) | 分层、Capability 系统、暴露模式 |
| [docs/proxy-session.md](docs/proxy-session.md) | 代理会话层契约（TTL / degraded / 写门控） |
| [docs/tool-reference.zh.md](docs/tool-reference.zh.md) / [English](docs/tool-reference.md) | Capability 参数手册（`py scripts/build_tool_reference.py` 中英同时生成） |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 新增 Capability、测试、打包、发版 |
| [Resources/CapabilitySpec.md](Resources/CapabilitySpec.md) | Capability 元数据规范 |
| [Resources/AIRules.mdc](Resources/AIRules.mdc) | IDE Rule 模板（复制到游戏项目，见 [usage-guide §2.8](docs/usage-guide.md#28-挂载-airules)） |
| [CHANGELOG.md](CHANGELOG.md) | 版本记录 |

## License

[MIT](LICENSE) © byteyang
