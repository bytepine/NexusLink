# NexusLink MCP · SearchMode

中文 | [English](InitializeInstructions.SearchMode.md)

直接调用元工具。能力名未知时，用 1–2 个精确关键词调用 `SearchCapabilities`。需要参数和前置条件时按确切的 `CapabilityName` 查询，已知动作时加 `Action`。执行时给 `CallCapability` 传入 `Capability` 和嵌套的 `Arguments` 对象。独立目标可用 `Calls[]` 批量调用。

用 MCP 核对 UE 状态。先用 `SearchAsset` 指定 `AssetType`、窄范围 `PathFilter` 和较小的 `Limit`，再使用返回的 `Assets[].Path` 和 `RecommendedGet` / `RecommendedManage`。一次请求所需的 `Sections` 和支持的 `PropertyPaths`。日志先过滤，首轮最多 50 条，之后仅补取缺失数据。先检查运行时前置条件。复现问题、捕获事件或验证操作结果时，先读工具说明，在触发动作前开启所需的观测。

需要确认时提供 `Reason`。遇到 `UserDenied` 即停止。检查 `isError` 和操作、分段、编译及保存错误。写入可能部分生效，超时或断连后先核实状态，再决定是否重新写入。收到 `FeedbackHint` 时调用 `SubmitFeedback`。
