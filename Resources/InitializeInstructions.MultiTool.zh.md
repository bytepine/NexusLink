# NexusLink MCP · MultiTool

中文 | [English](InitializeInstructions.MultiTool.md)

从 `tools/list` 查找当前宿主启用的能力。读取所选工具的说明、`inputSchema` 和前置条件，再用 `tools/call` 按工具名直接调用。每个目标单独调用一次。

用 MCP 核对 UE 状态。先用 `SearchAsset` 指定 `AssetType`、窄范围 `PathFilter` 和较小的 `Limit`，再使用返回的 `Assets[].Path` 和 `RecommendedGet` / `RecommendedManage`。一次请求所需的 `Sections` 和支持的 `PropertyPaths`。日志先过滤，首轮最多 50 条，之后仅补取缺失数据。先检查运行时前置条件。复现问题、捕获事件或验证操作结果时，先读工具说明，在触发动作前开启所需的观测。

需要确认时提供 `Reason`。遇到 `UserDenied` 即停止。检查 `isError` 和操作、分段、编译及保存错误。写入可能部分生效，超时或断连后先核实状态，再决定是否重新写入。收到 `FeedbackHint` 时调用 `SubmitFeedback`。
