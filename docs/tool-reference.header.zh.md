# NexusLink 工具参考手册

**语言 / Language**: **简体中文** · [English](tool-reference.md)

本文列出 NexusLink 暴露的全部 MCP 工具与 Capability，并附参数说明。

> **工具 vs Capability**
> - **MCP 工具（3 个）**：`SearchCapabilities`、`CallCapability`、`SubmitFeedback` — 经 MCP `tools/call` 直接调用。
> - **Capability（其余全部）**：经 `CallCapability`：单条 `Capability` + `Arguments`；或批量 `Calls=[{capability,arguments?},...]`。下文除元工具外每个 `###` 小节都是 Capability，标题即 `Capability` 字段值。
>
> 示例（单条）：`CallCapability(Capability="ListRuntimeActors", Arguments={"ClassFilter":"BP_Enemy"})`

> **`SearchCapabilities` 失败类型**（检查 `ErrorKind`；不要只看一条 `Error` 字符串）
> - `NotFound`：Capability 名不在注册表
> - `disabled`：精确 `CapabilityName` / cap 名 `Query` 命中设置中已禁用的 cap
> - `DisabledOnly`：模糊 `Query` 只匹配到**已禁用**的 cap（见 `DisabledCapabilities[]`）
> - `CallCapability` 另有 `UserDenied`：编辑器用户拒绝危险 cap（不要重试）

> **通用约定**
> - MCP / JSON-RPC 标准键保留协议拼写（`capabilities`、`instructions`、`inputSchema`、`annotations`、`code`、`message`）；PascalCase 用于 Capability 业务数据。
> - 操作、section、编译或保存错误返回 `isError=true` 并保留结构化详情。`Calls[]`（≥2 项）全部业务操作及自动编译通过后才自动保存，检查 `bRolledBack` / `UndoNote`；磁盘写入、显式包级操作及运行时副作用不保证原子性，也不整体回滚。单次 manage 部分失败仍保留成功操作。
> - 所有 `AssetPath` 均为 UE 内容路径，例如 `/Game/Blueprints/BP_Player`
> - 属性路径（`PropertyPath`）支持 `A.B.C` 点分下钻 + 容器下标 `[i]` / `["Key"]`：
>     - 数组：`Items[0]`、`Matrix[0][1]`
>     - Map：`Users["alice"].Score`（键与 `ExportText` 比较；字符串键会去掉引号）
>     - Set：`Tags["Player.Ally"]`
>     - 组合：`MyComp.RelativeLocation.X`、`Users["alice"].Modules[2].Name`
>     - 分段按 `.` 切分；Map 的字符串键**不能包含 `.`**
> - 过滤器支持子串（`Player`）、前缀（`^BP_`）、后缀（`Actor$`）、正则（`/^BP_.+$/`）
> - 列表类工具支持 `Offset`（默认 0）和 `Limit`（默认 100，最大 500）
> - ★ 表示必填参数
> - **manage 收尾**：所有 `manage_asset_*` 接受可选 `bSaveToDisk`（默认 false）；仅 `ManageAssetBlueprint` / `ManageAssetAnimBlueprint` / `ManageAssetUserWidget` 另接受 `bCompile`（默认 false）。UDS 改字段后仍会自动编译；材质用 `Recompile` 操作。独立的 `SaveAsset` / `CompileBlueprint` 仍可用
> - **单目标 + 跨目标批量（Breaking）**：Capability 一次处理一个目标（`AssetPath` / `ActorName` / `WidgetName`）；跨目标走 `CallCapability(Calls=[{capability,arguments?},...])`。同一目标内继续用 `Sections` / `PropertyPaths` / `Operations` / `Updates`。没有 `assetPaths`/`actorNames`/`widgetNames` 或旧键（`blueprintPath`→`AssetPath`、`newPath`→`DestAssetPath`、`ownerWidget`→`OwnerClass`、`FilePath`→`ScriptPath`、顶层 `Fields`/`Rows`/`Keys`/`Widgets`→`Operations`）；旧键 → `ArgInvalid`
> - **响应默认值压缩（全局开启）**：`NexusMcpDispatcher` 在序列化前递归扫描 `StructuredContent` 中的对象数组字段 `K`。仅当标量字段（string / number / bool / null）出现在**每一**条对象上才成为候选；主导值满足阈值（`MinCount=2` / `MinMatchRatio=0.7` / `MinNetSaveBytes=20`）时，在同级写入 `<K>_defaults` 并从条目中省略相等字段。稀疏字段（仅部分条目有）不抽取。身份字段（`name` / `Path` / `AssetPath` / `NodeId` / `Tag` / `Message` / `Timestamp` / `frame` / `Id` / `Label` / `Title` / `text` / `Error`）永不进入 defaults。ForcedDefault：带类型的 `SearchAsset` 抽取 `AssetType`；`GetOutputLog` 在 `verbosity≠all` 时抽取下限；`CategoryFilter` / `ListRuntimeActors.ClassFilter` / `ListRuntimeWidgets.ClassFilter` 等过滤器仅当**本页所有条目一致**时抽取（含 N=1；不要把过滤子串当默认值）。
>
>   **合并规则**：`merged = {**defaults, **entry}`；条目缺字段即等于 defaults。
>
>   **合并 / 跳过**：已有 `<K>_defaults` 时合并新键且不覆盖 ForcedDefault；跳过以 `_defaults` 结尾或等于 `Content` 的字段名。
>
>   开关：Editor → Editor Preferences → Plugins → NexusLink → `Compact response defaults`。

---
